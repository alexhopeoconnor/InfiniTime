#!/usr/bin/env python3
"""Summarise a JSONL session emitted by ble_hr_logger.py."""

from __future__ import annotations

import argparse
from datetime import datetime
import json
from pathlib import Path
import sys


def parse_time(value: str) -> datetime:
  return datetime.fromisoformat(value)


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("session", type=Path)
  args = parser.parse_args()

  try:
    events = [json.loads(line) for line in args.session.read_text(encoding="utf-8").splitlines() if line]
  except (OSError, json.JSONDecodeError) as error:
    print(f"cannot read session: {error}", file=sys.stderr)
    return 1

  starts = [event for event in events if event.get("event") == "session_start"]
  ends = [event for event in events if event.get("event") == "session_end"]
  measurements = [event for event in events if event.get("event") == "measurement"]
  connections = [event for event in events if event.get("event") == "connected"]
  losses = [event for event in events if event.get("event") == "connection_lost"]
  connection_errors = [event for event in events if event.get("event") == "connect_error"]
  recorder_errors = [
    event
    for event in events
    if event.get("event") in {"error", "subscribe_error", "stop_notify_error", "disconnect_error"}
  ]

  if not starts or not ends:
    print("session is incomplete: missing start or end event", file=sys.stderr)
    return 1

  start_time = parse_time(starts[0]["host_time_utc"])
  measurement_times = [parse_time(event["host_time_utc"]) for event in measurements]
  values = [event["bpm"] for event in measurements]
  first_value_seconds = (measurement_times[0] - start_time).total_seconds() if measurement_times else None
  gaps = [
    (later - earlier).total_seconds()
    for earlier, later in zip(measurement_times, measurement_times[1:])
  ]

  print(f"Session: {starts[0]['label']}")
  print(f"Duration: {ends[-1]['elapsed_seconds']:.1f}s")
  print(f"BLE HR notifications: {len(measurements)}")
  print("First received value: " + (f"{first_value_seconds:.1f}s" if first_value_seconds is not None else "none"))
  print("BPM range: " + (f"{min(values)}–{max(values)}" if values else "none"))
  print("Longest BLE event gap: " + (f"{max(gaps):.1f}s" if gaps else "n/a"))
  print(f"Successful BLE connections: {len(connections)}")
  print(f"Link-loss callbacks (including failed setup): {len(losses)}")
  print(f"Reconnect attempts that could not connect: {len(connection_errors)}")
  print(f"Recorder errors: {len(recorder_errors)}")
  print("Note: a BLE event gap is not necessarily a measurement failure; the current firmware may suppress unchanged BPM notifications.")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
