#!/usr/bin/env python3
"""Compare bounded JSONL heart-rate capture sessions without contacting a watch."""

from __future__ import annotations

import argparse
from datetime import datetime
import json
from pathlib import Path
import sys
from typing import Any


RECORDER_ERROR_EVENTS = {"error", "subscribe_error", "stop_notify_error", "disconnect_error"}


def parse_time(value: str) -> datetime:
  return datetime.fromisoformat(value)


def load_session(path: Path) -> list[dict[str, Any]]:
  try:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]
  except (OSError, json.JSONDecodeError) as error:
    raise ValueError(f"cannot read {path.name}: {error}") from error


def duration_between(start: datetime, end: datetime) -> float:
  return max(0.0, (end - start).total_seconds())


def link_coverage(events: list[dict[str, Any]], session_end: datetime) -> tuple[float | None, str]:
  """Return connected time for reconnect-aware sessions, else explain the limit."""
  if not any(event.get("event") == "connect_attempt" for event in events):
    return None, "legacy recorder: link-loss time unknown"

  opened: dict[int, datetime] = {}
  connected_seconds = 0.0
  for event in events:
    event_type = event.get("event")
    timestamp = parse_time(event["host_time_utc"])
    attempt = event.get("attempt")
    if event_type == "connected" and isinstance(attempt, int):
      opened[attempt] = timestamp
    elif event_type in {"connection_lost", "disconnected"} and isinstance(attempt, int):
      started = opened.pop(attempt, None)
      if started is not None:
        connected_seconds += duration_between(started, timestamp)

  for started in opened.values():
    connected_seconds += duration_between(started, session_end)
  return connected_seconds, ""


def session_summary(path: Path) -> dict[str, str]:
  events = load_session(path)
  starts = [event for event in events if event.get("event") == "session_start"]
  ends = [event for event in events if event.get("event") == "session_end"]
  if not starts or not ends:
    raise ValueError(f"{path.name}: missing session_start or session_end")

  start = parse_time(starts[0]["host_time_utc"])
  end = parse_time(ends[-1]["host_time_utc"])
  elapsed = float(ends[-1]["elapsed_seconds"])
  measurements = [event for event in events if event.get("event") == "measurement"]
  values = [int(event["bpm"]) for event in measurements]
  times = [parse_time(event["host_time_utc"]) for event in measurements]
  gaps = [duration_between(previous, current) for previous, current in zip(times, times[1:])]
  coverage_seconds, coverage_note = link_coverage(events, end)
  connected = sum(event.get("event") == "connected" for event in events)
  losses = sum(event.get("event") == "connection_lost" for event in events)
  connect_errors = sum(event.get("event") == "connect_error" for event in events)
  recorder_errors = sum(event.get("event") in RECORDER_ERROR_EVENTS for event in events)

  if coverage_seconds is None:
    coverage = "unknown"
    verdict = coverage_note
  else:
    coverage_percent = 100.0 * coverage_seconds / elapsed if elapsed else 0.0
    coverage = f"{coverage_percent:.0f}%"
    verdict = "clean link" if coverage_percent >= 95.0 and losses == 0 and connect_errors == 0 else "link unstable"

  return {
    "session": str(starts[0]["label"]),
    "duration": f"{elapsed:.0f}s",
    "coverage": coverage,
    "connections": str(connected),
    "losses": str(losses),
    "connect_errors": str(connect_errors),
    "notifications": str(len(measurements)),
    "bpm": f"{min(values)}–{max(values)}" if values else "none",
    "first": f"{duration_between(start, times[0]):.0f}s" if times else "none",
    "gap": f"{max(gaps):.0f}s" if gaps else "n/a",
    "errors": str(recorder_errors),
    "verdict": verdict,
  }


def print_table(rows: list[dict[str, str]]) -> None:
  columns = [
    ("session", "Session"),
    ("duration", "Duration"),
    ("coverage", "Link"),
    ("connections", "Conn"),
    ("losses", "Loss"),
    ("connect_errors", "Retry fail"),
    ("notifications", "HR events"),
    ("bpm", "BPM"),
    ("first", "First"),
    ("gap", "Max gap"),
    ("verdict", "Assessment"),
  ]
  widths = {
    key: max(len(title), *(len(row[key]) for row in rows))
    for key, title in columns
  }
  print("  ".join(title.ljust(widths[key]) for key, title in columns))
  print("  ".join("-" * widths[key] for key, _ in columns))
  for row in rows:
    print("  ".join(row[key].ljust(widths[key]) for key, _ in columns))


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("sessions", type=Path, nargs="+", help="two or more JSONL files from the same capture directory")
  args = parser.parse_args()
  if len(args.sessions) < 2:
    parser.error("provide at least two sessions")

  try:
    rows = [session_summary(path) for path in args.sessions]
  except ValueError as error:
    print(error, file=sys.stderr)
    return 1

  print_table(rows)
  if any(row["verdict"] != "clean link" for row in rows):
    print("\nComparison caution: do not score PPG quality from HR-event counts until a clean-link session exists.")
  print("HR events are changed standard-BLE values, not a complete on-watch sample history.")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
