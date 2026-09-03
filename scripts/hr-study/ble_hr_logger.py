#!/usr/bin/env python3
"""Record standard Bluetooth Heart Rate Measurement notifications.

The program is deliberately narrow: it connects to an existing BlueZ-paired
device, subscribes to 0x2A37, and appends received packets to JSON Lines.  It
does not scan, pair, modify watch settings, or expose a network service.
"""

from __future__ import annotations

import argparse
import asyncio
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
from time import monotonic
from typing import Any

from bleak import BleakClient
from bleak.backends.device import BLEDevice


HEART_RATE_MEASUREMENT = "00002a37-0000-1000-8000-00805f9b34fb"
RECONNECT_DELAY_SECONDS = 15.0


def utc_now() -> str:
  return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def decode_heart_rate(packet: bytearray) -> int:
  """Decode the mandatory BPM field of Bluetooth SIG Heart Rate Measurement."""
  if len(packet) < 2:
    raise ValueError(f"heart-rate packet is too short: {packet.hex()}")

  flags = packet[0]
  if flags & 0x01:
    if len(packet) < 3:
      raise ValueError(f"16-bit heart-rate packet is too short: {packet.hex()}")
    return int.from_bytes(packet[1:3], byteorder="little")
  return packet[1]


class JsonlWriter:
  def __init__(self, path: Path) -> None:
    self._file = path.open("x", encoding="utf-8")

  def write(self, event: dict[str, Any]) -> None:
    event["host_time_utc"] = utc_now()
    self._file.write(json.dumps(event, separators=(",", ":"), sort_keys=True) + "\n")
    self._file.flush()

  def close(self) -> None:
    self._file.close()


def bluez_device(address: str, adapter: str) -> BLEDevice:
  """Use BlueZ's existing paired-device object instead of triggering a scan."""
  object_path = f"/org/bluez/{adapter}/dev_{address.replace(':', '_')}"
  return BLEDevice(address, address, {"path": object_path}, -127)


async def record(address: str, adapter: str, duration_seconds: int, output: Path, label: str) -> int:
  output.parent.mkdir(parents=True, exist_ok=True)
  writer = JsonlWriter(output)
  started = monotonic()
  deadline = started + duration_seconds
  notification_count = 0
  values: list[int] = []
  connection_attempts = 0
  connection_losses = 0
  ever_connected = False

  writer.write(
    {
      "event": "session_start",
      "address": address,
      "adapter": adapter,
      "duration_seconds": duration_seconds,
      "label": label,
      "characteristic": HEART_RATE_MEASUREMENT,
    }
  )

  def on_measurement(_: int, packet: bytearray) -> None:
    nonlocal notification_count
    try:
      bpm = decode_heart_rate(packet)
      notification_count += 1
      values.append(bpm)
      writer.write({"event": "measurement", "packet_hex": packet.hex(), "bpm": bpm})
    except Exception as error:  # callback exceptions must not stop BlueZ notifications
      writer.write({"event": "decode_error", "packet_hex": packet.hex(), "error": str(error)})

  try:
    while (remaining_seconds := deadline - monotonic()) > 0:
      connection_attempts += 1
      disconnected = asyncio.Event()
      intentional_disconnect = False
      loss_logged = False
      connected = False
      subscribed = False
      client: BleakClient | None = None
      loop = asyncio.get_running_loop()

      def on_disconnect(_: BleakClient) -> None:
        nonlocal connection_losses, loss_logged
        if intentional_disconnect or loss_logged:
          return
        loss_logged = True
        connection_losses += 1
        writer.write({"event": "connection_lost", "attempt": connection_attempts})
        loop.call_soon_threadsafe(disconnected.set)

      try:
        writer.write({"event": "connect_attempt", "attempt": connection_attempts})
        client = BleakClient(bluez_device(address, adapter), disconnected_callback=on_disconnect)
        await client.connect(timeout=min(20.0, remaining_seconds))
        connected = True
        ever_connected = True
        writer.write({"event": "connected", "attempt": connection_attempts})

        await client.start_notify(HEART_RATE_MEASUREMENT, on_measurement)
        subscribed = True
        writer.write({"event": "subscribed", "attempt": connection_attempts})

        try:
          await asyncio.wait_for(disconnected.wait(), timeout=max(0.0, deadline - monotonic()))
        except asyncio.TimeoutError:
          break
      except Exception as error:
        event = "subscribe_error" if connected else "connect_error"
        writer.write({"event": event, "attempt": connection_attempts, "error": f"{type(error).__name__}: {error}"})
      finally:
        intentional_disconnect = True
        if client is not None and client.is_connected:
          if subscribed:
            try:
              await client.stop_notify(HEART_RATE_MEASUREMENT)
              writer.write({"event": "unsubscribed", "attempt": connection_attempts})
            except Exception as error:
              writer.write({"event": "stop_notify_error", "attempt": connection_attempts, "error": f"{type(error).__name__}: {error}"})
          try:
            await client.disconnect()
            writer.write({"event": "disconnected", "attempt": connection_attempts})
          except Exception as error:
            writer.write({"event": "disconnect_error", "attempt": connection_attempts, "error": f"{type(error).__name__}: {error}"})

      remaining_seconds = deadline - monotonic()
      if remaining_seconds > 0:
        retry_delay = min(RECONNECT_DELAY_SECONDS, remaining_seconds)
        writer.write({"event": "reconnect_wait", "seconds": round(retry_delay, 3)})
        await asyncio.sleep(retry_delay)
    return 0 if ever_connected else 1
  finally:
    elapsed_seconds = monotonic() - started
    writer.write(
      {
        "event": "session_end",
        "elapsed_seconds": round(elapsed_seconds, 3),
        "notification_count": notification_count,
        "bpm_min": min(values) if values else None,
        "bpm_max": max(values) if values else None,
        "connection_attempts": connection_attempts,
        "connection_losses": connection_losses,
      }
    )
    writer.close()


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  subcommands = parser.add_subparsers(dest="command", required=True)
  record_parser = subcommands.add_parser("record", help="record notifications for a bounded session")
  record_parser.add_argument("--address", required=True, help="existing BlueZ-paired device address")
  record_parser.add_argument("--adapter", default="hci0", help="BlueZ adapter which owns the paired device")
  record_parser.add_argument("--duration", type=int, required=True, help="bounded recording duration in seconds")
  record_parser.add_argument("--out", type=Path, required=True, help="new JSONL output path")
  record_parser.add_argument("--label", required=True, help="human-readable session label")
  return parser.parse_args()


def main() -> int:
  args = parse_args()
  if args.duration <= 0:
    print("--duration must be positive", file=sys.stderr)
    return 2
  return asyncio.run(record(args.address, args.adapter, args.duration, args.out, args.label))


if __name__ == "__main__":
  raise SystemExit(main())
