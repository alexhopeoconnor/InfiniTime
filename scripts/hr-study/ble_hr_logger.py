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
from datetime import datetime, timezone
import json
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
STUDY_START = b"\x01"
STUDY_STOP = b"\x02"
RECONNECT_DELAY_SECONDS = 15.0
RECORD_STRUCT = struct.Struct("<IIHHHBBBBBB")
SCHEMA_VERSION = 1

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

  def close(self) -> None:
    self._file.close()


def bluez_device(address: str, adapter: str) -> BLEDevice:
  """Use BlueZ's existing paired-device object instead of starting a scan."""
  object_path = f"/org/bluez/{adapter}/dev_{address.replace(':', '_')}"
  return BLEDevice(address, address, {"path": object_path}, -127)


def decode_record(packet: bytearray) -> dict[str, int | str | bool]:
  if len(packet) != RECORD_STRUCT.size:
    raise ValueError(f"study record must be {RECORD_STRUCT.size} bytes, got {len(packet)}: {packet.hex()}")

  sequence, watch_tick, ppg_mean, ppg_range, motion_level, bpm, outcome, flags, step_delta, ambient, profile_drive = (
      RECORD_STRUCT.unpack(packet)
  )
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
      "step_delta": step_delta,
      "ambient_level": ambient,
      "algorithm_profile": profile_drive & 0x03,
      "sensor_drive": profile_drive >> 2,
      "packet_hex": packet.hex(),
  }


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


async def record(address: str, adapter: str, duration_seconds: int, output: Path, label: str, algorithm: str) -> int:
  output.parent.mkdir(parents=True, exist_ok=True)
  writer = JsonlWriter(output)
  started = monotonic()
  deadline = started + duration_seconds
  connection_attempts = 0
  connection_losses = 0
  ever_connected = False
  study_started = False
  received_records = 0
  accepted_records = 0
  recovered_records = 0
  duplicate_records = 0
  seen_sequences: set[int] = set()

  writer.write(
      {
          "event": "session_start",
          "schema_version": SCHEMA_VERSION,
          "address": address,
          "adapter": adapter,
          "duration_seconds": duration_seconds,
          "label": label,
          "algorithm": algorithm,
          "watch_tick_hz": 1024,
          "study_service": STUDY_SERVICE,
          "record_characteristic": STUDY_RECORD,
      }
  )

  try:
    while (remaining_seconds := deadline - monotonic()) > 0:
      connection_attempts += 1
      attempt = connection_attempts
      disconnected = asyncio.Event()
      intentional = {"value": False}
      loss_logged = {"value": False}
      connected = False
      subscribed = False
      client: BleakClient | None = None
      recovered_connection = ever_connected

      def on_loss() -> None:
        nonlocal connection_losses
        connection_losses += 1

      def on_record(_: int, packet: bytearray) -> None:
        nonlocal received_records, accepted_records, recovered_records, duplicate_records
        try:
          decoded = decode_record(packet)
          sequence = int(decoded["sequence"])
          if sequence in seen_sequences:
            duplicate_records += 1
            writer.write({"event": "duplicate_record", "attempt": attempt, **decoded})
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
              }
          )
        except Exception as error:  # Do not let a malformed test packet kill BlueZ callbacks.
          writer.write({"event": "decode_error", "attempt": attempt, "packet_hex": packet.hex(), "error": str(error)})

      try:
        writer.write({"event": "connect_attempt", "attempt": attempt})
        client = BleakClient(
            bluez_device(address, adapter),
            disconnected_callback=make_disconnect_callback(attempt, intentional, loss_logged, disconnected, writer, on_loss),
        )
        await client.connect(timeout=min(20.0, remaining_seconds))
        connected = True
        ever_connected = True
        writer.write({"event": "connected", "attempt": attempt})

        await client.start_notify(STUDY_RECORD, on_record)
        subscribed = True
        writer.write({"event": "indications_subscribed", "attempt": attempt})

        if not study_started:
          await client.write_gatt_char(STUDY_CONTROL, STUDY_START, response=True)
          study_started = True
          writer.write({"event": "study_started", "attempt": attempt})

        try:
          await asyncio.wait_for(disconnected.wait(), timeout=max(0.0, deadline - monotonic()))
        except asyncio.TimeoutError:
          break
      except Exception as error:
        event = "subscribe_error" if connected else "connect_error"
        writer.write({"event": event, "attempt": attempt, "error": f"{type(error).__name__}: {error}"})
      finally:
        intentional["value"] = True
        if client is not None and client.is_connected:
          if subscribed:
            try:
              if study_started and deadline - monotonic() <= 0:
                await client.write_gatt_char(STUDY_CONTROL, STUDY_STOP, response=True)
                writer.write({"event": "study_stopped", "attempt": attempt})
              await client.stop_notify(STUDY_RECORD)
              writer.write({"event": "indications_unsubscribed", "attempt": attempt})
            except Exception as error:
              writer.write({"event": "stop_notify_error", "attempt": attempt, "error": f"{type(error).__name__}: {error}"})
          try:
            await client.disconnect()
            writer.write({"event": "disconnected", "attempt": attempt})
          except Exception as error:
            writer.write({"event": "disconnect_error", "attempt": attempt, "error": f"{type(error).__name__}: {error}"})

      remaining_seconds = deadline - monotonic()
      if remaining_seconds > 0:
        retry_delay = min(RECONNECT_DELAY_SECONDS, remaining_seconds)
        writer.write({"event": "reconnect_wait", "seconds": round(retry_delay, 3)})
        await asyncio.sleep(retry_delay)
    return 0 if ever_connected else 1
  finally:
    writer.write(
        {
            "event": "session_end",
            "elapsed_seconds": round(monotonic() - started, 3),
            "received_records": received_records,
            "accepted_records": accepted_records,
            "recovered_records": recovered_records,
            "duplicate_records": duplicate_records,
            "connection_attempts": connection_attempts,
            "connection_losses": connection_losses,
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
  record_parser.add_argument("--out", type=Path, required=True, help="new JSONL output path")
  record_parser.add_argument("--label", required=True, help="human-readable session label")
  record_parser.add_argument("--algorithm", required=True, choices=("baseline", "ppgv2"), help="firmware estimator under test")
  return parser.parse_args()


def main() -> int:
  args = parse_args()
  if args.duration <= 0:
    print("--duration must be positive", file=sys.stderr)
    return 2
  return asyncio.run(record(args.address, args.adapter, args.duration, args.out, args.label, args.algorithm))


if __name__ == "__main__":
  raise SystemExit(main())
