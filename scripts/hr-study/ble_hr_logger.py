#!/usr/bin/env python3
"""Record temporary ElixirTime HR-study indications from a paired PineTime.

The program has no network listener, does not scan or pair, and does not alter
watch settings. It uses an existing BlueZ device object, asks the temporary
study service to start one bounded session, and reconnects after normal range
loss. The watch buffers records while disconnected and flushes them as BLE
indications after a new subscription is established.
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import struct
import sys
from time import monotonic
from typing import Any, Callable

from bleak import BleakClient
from bleak.backends.device import BLEDevice


STUDY_SERVICE = "b1e3c2d0-6f14-4d72-9b91-48c4d1a0e401"
STUDY_CONTROL = "b1e3c2d0-6f14-4d72-9b91-48c4d1a0e402"
STUDY_RECORD = "b1e3c2d0-6f14-4d72-9b91-48c4d1a0e403"
STUDY_DIAGNOSTICS = "b1e3c2d0-6f14-4d72-9b91-48c4d1a0e404"
STUDY_START = 0x01
STUDY_STOP = b"\x02"
STUDY_FLUSH = b"\x03"
STUDY_ACKNOWLEDGE = b"\x04"
STUDY_START_STRUCT = struct.Struct("<BH")
RECONNECT_DELAY_SECONDS = 15.0
DEFAULT_RETURN_GRACE_SECONDS = 15 * 60
MAX_STUDY_CAPTURE_SECONDS = 45 * 60
DRAIN_QUIESCENCE_SECONDS = 1.0
DRAIN_TIMEOUT_SECONDS = 20.0
SUMMARY_STRUCT = struct.Struct("<BBBHIHHHBBBH")
DIAGNOSTICS_STRUCT = struct.Struct("<BBBHHBBBbbbBBBHH")
ACK_STRUCT = struct.Struct("<BI")
SCHEMA_VERSION = 4
PROTOCOL_MARKER = 0xE4
FRAME_KIND_SUMMARY = 0x01
FRAME_KIND_DIAGNOSTICS = 0x02

OUTCOMES = {
    1: "accepted",
    2: "not_enough_data",
    3: "signal_unstable",
    4: "ambient_light",
    5: "no_touch",
    6: "sensor_error",
    7: "interrupted",
}


def utc_now() -> str:
  return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


class JsonlWriter:
  def __init__(self, path: Path) -> None:
    self._file = path.open("x", encoding="utf-8")

  def write(self, event: dict[str, Any]) -> None:
    timestamp = utc_now()
    event["host_time_utc"] = timestamp
    if event.get("event") == "measurement":
      event["arrival_utc"] = timestamp
    self._file.write(json.dumps(event, separators=(",", ":"), sort_keys=True) + "\n")
    self._file.flush()
    # The watch keeps its FIFO entry until the recorder ACKs its sequence.
    # A Python flush alone only reaches the kernel page cache; force it to
    # storage before an acknowledgement can be sent for this event.
    os.fsync(self._file.fileno())

  def close(self) -> None:
    self._file.close()


def bluez_device(address: str, adapter: str) -> BLEDevice:
  """Use BlueZ's existing paired-device object instead of starting a scan."""
  object_path = f"/org/bluez/{adapter}/dev_{address.replace(':', '_')}"
  return BLEDevice(address, address, {"path": object_path}, -127)


def crc8_atm(packet: bytes | bytearray) -> int:
  """CRC-8/ATM used by protocol v4; byte 2 is the checksum field itself."""
  crc = 0
  for index, byte in enumerate(packet):
    crc ^= 0 if index == 2 else byte
    for _ in range(8):
      crc = (crc << 1) & 0xFF if not (crc & 0x80) else ((crc << 1) & 0xFF) ^ 0x07
  return crc


def validate_frame(packet: bytearray, expected_kind: int, expected_size: int) -> tuple[int, int]:
  if len(packet) != expected_size:
    raise ValueError(f"study frame must be {expected_size} bytes, got {len(packet)}: {packet.hex()}")
  marker, kind_and_flags, checksum = packet[:3]
  if marker != PROTOCOL_MARKER:
    raise ValueError(f"unexpected study protocol marker 0x{marker:02x}")
  kind = kind_and_flags >> 5
  if kind != expected_kind:
    raise ValueError(f"unexpected study frame kind {kind}, expected {expected_kind}")
  if checksum != crc8_atm(packet):
    raise ValueError("study frame checksum mismatch")
  return kind_and_flags & 0x1F, checksum


def decode_summary(packet: bytearray) -> dict[str, int | str | bool]:
  flags, _ = validate_frame(packet, FRAME_KIND_SUMMARY, SUMMARY_STRUCT.size)
  _, _, _, sequence, watch_tick, ppg_mean, ppg_range, motion_level, bpm, outcome, sensor_configuration, ambient_maximum = SUMMARY_STRUCT.unpack(packet)
  if outcome not in OUTCOMES:
    raise ValueError(f"unknown study outcome {outcome}")
  if flags & 0x03 not in (0x01, 0x02):
    raise ValueError(f"invalid study measurement-mode flags 0x{flags:02x}")
  sensor_drive = sensor_configuration & 0x07
  sensor_gain = (sensor_configuration >> 3) & 0x03
  sensor_state = (sensor_configuration >> 5) & 0x03
  return {
      "sequence": sequence,
      "watch_tick": watch_tick,
      "ppg_mean": ppg_mean,
      "ppg_range": ppg_range,
      "motion_level": motion_level,
      "bpm": bpm,
      "outcome_code": outcome,
      "outcome": OUTCOMES.get(outcome, f"unknown_{outcome}"),
      "background_measurement": bool(flags & 0x01),
      "foreground_measurement": bool(flags & 0x02),
      "power_present": bool(flags & 0x04),
      "algorithm_profile": 1 if flags & 0x08 else 0,
      "sensor_drive": sensor_drive,
      "sensor_gain": sensor_gain,
      "sensor_state": sensor_state,
      "ambient_maximum": ambient_maximum,
      "packet_hex": packet.hex(),
  }


def decode_diagnostics(packet: bytearray) -> dict[str, int | bool | None]:
  validate_frame(packet, FRAME_KIND_DIAGNOSTICS, DIAGNOSTICS_STRUCT.size)
  (
      _,
      _,
      _,
      sequence,
      sample_count,
      analysis_count,
      strongest_bin,
      locked_bin,
      strongest_energy_tenths,
      locked_energy_tenths,
      rolling_energy_tenths,
      ppg_flags,
      sensor_transitions,
      queue_drops,
      stack_high_water_words,
      minimum_free_heap_bytes,
  ) = DIAGNOSTICS_STRUCT.unpack(packet)
  if ppg_flags & ~0x07:
    raise ValueError(f"invalid PPG diagnostic flags 0x{ppg_flags:02x}")

  def energy(value: int) -> float | None:
    return None if value == -128 else value / 10.0

  return {
      "sequence": sequence,
      "ppg_sample_count": sample_count,
      "ppg_analysis_count": analysis_count,
      "ppg_strongest_bin": strongest_bin,
      "ppg_locked_bin": None if locked_bin == 0xFF else locked_bin,
      "ppg_strongest_energy": energy(strongest_energy_tenths),
      "ppg_locked_energy": energy(locked_energy_tenths),
      "ppg_rolling_energy": energy(rolling_energy_tenths),
      "ppg_sufficient_data": bool(ppg_flags & 0x01),
      "ppg_strong_candidate": bool(ppg_flags & 0x02),
      "ppg_lock_active": bool(ppg_flags & 0x04),
      "sensor_transitions": sensor_transitions,
      "heart_rate_queue_drops": queue_drops,
      "heart_rate_stack_high_water_words": stack_high_water_words,
      "minimum_free_heap_bytes": minimum_free_heap_bytes,
      "diagnostics_packet_hex": packet.hex(),
  }


def make_study_start(duration_seconds: int) -> bytes:
  """Encode the temporary service's bounded on-watch capture lease."""
  if not 0 < duration_seconds <= MAX_STUDY_CAPTURE_SECONDS:
    raise ValueError(f"study duration must be between 1 and {MAX_STUDY_CAPTURE_SECONDS} seconds")
  return STUDY_START_STRUCT.pack(STUDY_START, duration_seconds)


def make_disconnect_callback(
    attempt: int,
    intentional: dict[str, bool],
    loss_logged: dict[str, bool],
    disconnected: asyncio.Event,
    writer: JsonlWriter,
    on_loss: Callable[[], None],
) -> Callable[[BleakClient], None]:
  """Bind every reconnect attempt to its own callback state.

  A previous logger captured loop variables that could be reassigned before a
  late BlueZ disconnect callback ran. This factory prevents a loss from one
  connection being attributed to the next attempt.
  """

  loop = asyncio.get_running_loop()

  def on_disconnect(_: BleakClient) -> None:
    if intentional["value"] or loss_logged["value"]:
      return
    loss_logged["value"] = True
    on_loss()
    writer.write({"event": "connection_lost", "attempt": attempt})
    loop.call_soon_threadsafe(disconnected.set)

  return on_disconnect


async def acknowledge_records(
    client: BleakClient,
    acknowledgements: asyncio.Queue[int],
    writer: JsonlWriter,
    attempt: int,
    disconnected: asyncio.Event,
    acknowledgement_failed: asyncio.Event,
) -> None:
  """Acknowledge only records that have been synchronously flushed to JSONL."""

  while True:
    sequence = await acknowledgements.get()
    try:
      await client.write_gatt_char(STUDY_CONTROL, ACK_STRUCT.pack(STUDY_ACKNOWLEDGE[0], sequence), response=True)
      writer.write({"event": "record_acknowledged", "attempt": attempt, "sequence": sequence})
    except asyncio.CancelledError:
      raise
    except Exception as error:
      writer.write(
          {
              "event": "record_ack_error",
              "attempt": attempt,
              "sequence": sequence,
              "error": f"{type(error).__name__}: {error}",
          }
      )
      # An empty queue is not evidence of a durable acknowledgement. Keep the
      # watch FIFO intact, then let the reconnect path FLUSH this front record
      # again rather than allowing the session-end STOP to discard it.
      acknowledgement_failed.set()
      disconnected.set()
      return
    finally:
      acknowledgements.task_done()


async def wait_for_durable_drain(
    acknowledgements: asyncio.Queue[int],
    last_record_at: dict[str, float],
    acknowledgement_failed: asyncio.Event,
    protocol_fault: asyncio.Event,
    pending_summaries: dict[int, dict[str, Any]],
) -> bool:
  """Wait until acknowledgements are durable and no flushed record arrives.

  A study FLUSH releases one indication at a time; the watch sends the next
  only after its predecessor is acknowledged. A short quiet period after the
  ACK queue drains therefore proves the currently buffered FIFO has emptied
  without inventing a second protocol message solely for test-session stop.
  """

  deadline = monotonic() + DRAIN_TIMEOUT_SECONDS
  while monotonic() < deadline:
    if acknowledgement_failed.is_set() or protocol_fault.is_set():
      return False
    try:
      await asyncio.wait_for(acknowledgements.join(), timeout=0.25)
    except asyncio.TimeoutError:
      continue
    if not pending_summaries and monotonic() - last_record_at["value"] >= DRAIN_QUIESCENCE_SECONDS:
      return True
    await asyncio.sleep(0.05)
  return False


async def record(
    address: str,
    adapter: str,
    duration_seconds: int,
    return_grace_seconds: int,
    output: Path,
    label: str,
    algorithm: str,
) -> int:
  output.parent.mkdir(parents=True, exist_ok=True)
  writer = JsonlWriter(output)
  started = monotonic()
  collection_deadline = started + duration_seconds
  recovery_deadline = collection_deadline + return_grace_seconds
  connection_attempts = 0
  connection_losses = 0
  ever_connected = False
  study_started = False
  received_records = 0
  accepted_records = 0
  recovered_records = 0
  duplicate_records = 0
  protocol_errors = 0
  seen_sequences: set[int] = set()
  pending_summaries: dict[int, dict[str, Any]] = {}
  collection_complete = False
  target_elapsed_logged = False

  def mark_target_elapsed(stage: str) -> None:
    nonlocal target_elapsed_logged
    if target_elapsed_logged:
      return
    target_elapsed_logged = True
    writer.write(
        {
            "event": "collection_target_elapsed",
            "stage": stage,
            "target_duration_seconds": duration_seconds,
            "return_grace_seconds": return_grace_seconds,
        }
    )

  writer.write(
      {
          "event": "session_start",
          "schema_version": SCHEMA_VERSION,
          "address": address,
          "adapter": adapter,
          "duration_seconds": duration_seconds,
          "watch_capture_lease_seconds": duration_seconds,
          "watch_capture_lease_max_seconds": MAX_STUDY_CAPTURE_SECONDS,
          "return_grace_seconds": return_grace_seconds,
          "label": label,
          "algorithm": algorithm,
          "watch_tick_hz": 1024,
          "study_protocol_version": 4,
          "study_service": STUDY_SERVICE,
          "record_characteristic": STUDY_RECORD,
          "diagnostics_characteristic": STUDY_DIAGNOSTICS,
      }
  )

  try:
    while (remaining_recovery_seconds := recovery_deadline - monotonic()) > 0:
      connection_attempts += 1
      attempt = connection_attempts
      target_elapsed = monotonic() >= collection_deadline
      if target_elapsed:
        mark_target_elapsed("awaiting_reconnect")
      disconnected = asyncio.Event()
      intentional = {"value": False}
      loss_logged = {"value": False}
      connected = False
      subscribed_characteristics: list[str] = []
      client: BleakClient | None = None
      acknowledgements: asyncio.Queue[int] = asyncio.Queue()
      acknowledgement_failed = asyncio.Event()
      acknowledgement_task: asyncio.Task[None] | None = None
      recovered_connection = ever_connected
      last_record_at = {"value": monotonic()}
      protocol_fault = asyncio.Event()

      def on_loss() -> None:
        nonlocal connection_losses
        connection_losses += 1

      def mark_protocol_fault(frame: str, packet: bytearray, error: Exception | str) -> None:
        nonlocal protocol_errors
        protocol_errors += 1
        writer.write(
            {
                "event": "protocol_integrity_error",
                "attempt": attempt,
                "frame": frame,
                "packet_hex": packet.hex(),
                "error": str(error),
            }
        )
        # Do not ACK a frame whose integrity cannot be proven. The normal
        # path below sends STOP, deliberately discarding this invalid study
        # rather than allowing a corrupted FIFO entry to be reused as data.
        protocol_fault.set()
        disconnected.set()

      def on_summary(_: int, packet: bytearray) -> None:
        last_record_at["value"] = monotonic()
        try:
          decoded = decode_summary(packet)
          sequence = int(decoded["sequence"])
          if sequence in seen_sequences:
            # A lost ACK makes the watch replay the pair. Keep the summary
            # until its matching diagnostic frame arrives, then ACK it again.
            pending_summaries[sequence] = decoded
            writer.write({"event": "duplicate_summary", "attempt": attempt, **decoded})
            return
          previous = pending_summaries.get(sequence)
          if previous is not None:
            if previous["packet_hex"] != decoded["packet_hex"]:
              raise ValueError(f"summary sequence {sequence} changed before diagnostics arrived")
            writer.write({"event": "duplicate_summary", "attempt": attempt, **decoded})
            return
          pending_summaries[sequence] = decoded
        except Exception as error:  # Keep BlueZ callbacks alive long enough to stop an invalid study cleanly.
          mark_protocol_fault("summary", packet, error)

      def on_diagnostics(_: int, packet: bytearray) -> None:
        nonlocal received_records, accepted_records, recovered_records, duplicate_records
        last_record_at["value"] = monotonic()
        try:
          diagnostics = decode_diagnostics(packet)
          sequence = int(diagnostics["sequence"])
          decoded = pending_summaries.pop(sequence, None)
          if decoded is None:
            raise ValueError(f"diagnostics sequence {sequence} arrived without a matching summary")
          if sequence in seen_sequences:
            duplicate_records += 1
            writer.write({"event": "duplicate_record", "attempt": attempt, **decoded, **diagnostics})
            acknowledgements.put_nowait(sequence)
            return
          seen_sequences.add(sequence)
          received_records += 1
          if decoded["outcome"] == "accepted":
            accepted_records += 1
          if recovered_connection:
            recovered_records += 1
          writer.write(
              {
                  "event": "measurement",
                  "attempt": attempt,
                  "recovered_after_disconnect": recovered_connection,
                  "algorithm": algorithm,
                  **decoded,
                  **diagnostics,
              }
          )
          # JsonlWriter.write() flushes before this queue entry is visible to
          # the ACK worker. The watch will keep the front record until this
          # write-with-response succeeds.
          acknowledgements.put_nowait(sequence)
        except Exception as error:  # Do not let a malformed test packet kill BlueZ callbacks.
          mark_protocol_fault("diagnostics", packet, error)

      try:
        writer.write({"event": "connect_attempt", "attempt": attempt})
        client = BleakClient(
            bluez_device(address, adapter),
            disconnected_callback=make_disconnect_callback(attempt, intentional, loss_logged, disconnected, writer, on_loss),
        )
        await client.connect(timeout=min(20.0, remaining_recovery_seconds))
        connected = True
        ever_connected = True
        writer.write({"event": "connected", "attempt": attempt})
        acknowledgement_task = asyncio.create_task(
            acknowledge_records(client, acknowledgements, writer, attempt, disconnected, acknowledgement_failed)
        )

        await client.start_notify(STUDY_RECORD, on_summary)
        subscribed_characteristics.append(STUDY_RECORD)
        await client.start_notify(STUDY_DIAGNOSTICS, on_diagnostics)
        subscribed_characteristics.append(STUDY_DIAGNOSTICS)
        writer.write({"event": "indications_subscribed", "attempt": attempt, "frames": ("summary", "diagnostics")})

        target_elapsed = monotonic() >= collection_deadline
        if not study_started:
          if target_elapsed:
            mark_target_elapsed("collection_never_started")
            return 1
          await client.write_gatt_char(STUDY_CONTROL, make_study_start(duration_seconds), response=True)
          study_started = True
          writer.write({"event": "study_started", "attempt": attempt})
        else:
          # The watch intentionally does not drain merely because BlueZ
          # updated the CCCD. This request happens only after the callback and
          # durable-ACK worker are ready for recovered indications.
          await client.write_gatt_char(STUDY_CONTROL, STUDY_FLUSH, response=True)
          # A FLUSH can be the first operation after a slow reconnect.  The
          # quiet period must begin at this acknowledged request, not at the
          # start of the connection attempt, otherwise the host can decide
          # that an already-buffered FIFO is empty before its first indication
          # reaches the callback.
          last_record_at["value"] = monotonic()
          writer.write({"event": "study_flush_requested", "attempt": attempt})

        if target_elapsed:
          mark_target_elapsed("reconnected_for_drain")
          drained = await wait_for_durable_drain(acknowledgements, last_record_at, acknowledgement_failed, protocol_fault, pending_summaries)
          writer.write({"event": "soft_end_drain", "attempt": attempt, "drained": drained})
          if protocol_fault.is_set():
            try:
              await client.write_gatt_char(STUDY_CONTROL, STUDY_STOP, response=True)
              study_started = False
              writer.write({"event": "study_stopped_after_protocol_error", "attempt": attempt})
            except Exception as error:
              writer.write({"event": "stop_after_protocol_error", "attempt": attempt, "error": f"{type(error).__name__}: {error}"})
            return 1
          if drained:
            await client.write_gatt_char(STUDY_CONTROL, STUDY_STOP, response=True)
            study_started = False
            collection_complete = True
            writer.write({"event": "study_stopped", "attempt": attempt})
            return 0
          # Keep the study active. The finally block releases this damaged
          # connection and the next attempt will subscribe and FLUSH again.
          continue

        try:
          await asyncio.wait_for(disconnected.wait(), timeout=max(0.0, collection_deadline - monotonic()))
        except asyncio.TimeoutError:
          mark_target_elapsed("connected_at_target")
          drained = await wait_for_durable_drain(acknowledgements, last_record_at, acknowledgement_failed, protocol_fault, pending_summaries)
          writer.write({"event": "soft_end_drain", "attempt": attempt, "drained": drained})
          if drained:
            await client.write_gatt_char(STUDY_CONTROL, STUDY_STOP, response=True)
            study_started = False
            collection_complete = True
            writer.write({"event": "study_stopped", "attempt": attempt})
            return 0
        if protocol_fault.is_set():
          try:
            await client.write_gatt_char(STUDY_CONTROL, STUDY_STOP, response=True)
            study_started = False
            writer.write({"event": "study_stopped_after_protocol_error", "attempt": attempt})
          except Exception as error:
            writer.write({"event": "stop_after_protocol_error", "attempt": attempt, "error": f"{type(error).__name__}: {error}"})
          return 1
      except Exception as error:
        event = "subscribe_error" if connected else "connect_error"
        writer.write({"event": event, "attempt": attempt, "error": f"{type(error).__name__}: {error}"})
      finally:
        intentional["value"] = True
        if client is not None and client.is_connected:
          for characteristic in reversed(subscribed_characteristics):
            try:
              await client.stop_notify(characteristic)
              writer.write({"event": "indications_unsubscribed", "attempt": attempt, "characteristic": characteristic})
            except Exception as error:
              writer.write(
                  {"event": "stop_notify_error", "attempt": attempt, "characteristic": characteristic, "error": f"{type(error).__name__}: {error}"}
              )
          try:
            await client.disconnect()
            writer.write({"event": "disconnected", "attempt": attempt})
          except Exception as error:
            writer.write({"event": "disconnect_error", "attempt": attempt, "error": f"{type(error).__name__}: {error}"})
        if acknowledgement_task is not None:
          acknowledgement_task.cancel()
          with contextlib.suppress(asyncio.CancelledError):
            await acknowledgement_task

      remaining_recovery_seconds = recovery_deadline - monotonic()
      if remaining_recovery_seconds > 0:
        retry_delay = min(RECONNECT_DELAY_SECONDS, remaining_recovery_seconds)
        writer.write({"event": "reconnect_wait", "seconds": round(retry_delay, 3)})
        await asyncio.sleep(retry_delay)
    return 0 if collection_complete else 1
  finally:
    writer.write(
        {
            "event": "session_end",
            "elapsed_seconds": round(monotonic() - started, 3),
            "received_records": received_records,
            "accepted_records": accepted_records,
            "recovered_records": recovered_records,
            "duplicate_records": duplicate_records,
            "protocol_errors": protocol_errors,
            "connection_attempts": connection_attempts,
            "connection_losses": connection_losses,
            "target_duration_seconds": duration_seconds,
            "return_grace_seconds": return_grace_seconds,
            "collection_complete": collection_complete,
            "recovery_grace_expired": not collection_complete and monotonic() >= recovery_deadline,
        }
    )
    writer.close()


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  subcommands = parser.add_subparsers(dest="command", required=True)
  record_parser = subcommands.add_parser("record", help="record one bounded temporary study session")
  record_parser.add_argument("--address", required=True, help="existing BlueZ-paired device address")
  record_parser.add_argument("--adapter", default="hci0", help="BlueZ adapter that owns the paired device")
  record_parser.add_argument("--duration", type=int, required=True, help="bounded recording duration in seconds")
  record_parser.add_argument(
      "--return-grace",
      type=int,
      default=DEFAULT_RETURN_GRACE_SECONDS,
      help="seconds to keep reconnecting after the target duration (default: 900)",
  )
  record_parser.add_argument("--out", type=Path, required=True, help="new JSONL output path")
  record_parser.add_argument("--label", required=True, help="human-readable session label")
  record_parser.add_argument("--algorithm", required=True, choices=("baseline", "ppgv2"), help="firmware estimator under test")
  return parser.parse_args()


def main() -> int:
  args = parse_args()
  if not 0 < args.duration <= MAX_STUDY_CAPTURE_SECONDS:
    print(f"--duration must be between 1 and {MAX_STUDY_CAPTURE_SECONDS} seconds", file=sys.stderr)
    return 2
  if args.return_grace < 0:
    print("--return-grace must be zero or positive", file=sys.stderr)
    return 2
  return asyncio.run(
      record(args.address, args.adapter, args.duration, args.return_grace, args.out, args.label, args.algorithm)
  )


if __name__ == "__main__":
  raise SystemExit(main())
