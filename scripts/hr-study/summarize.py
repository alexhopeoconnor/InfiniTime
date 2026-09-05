#!/usr/bin/env python3
"""Summarise one temporary ElixirTime HR-study JSONL session."""

from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime
import json
from pathlib import Path
import statistics
import sys
from typing import Any


def load_events(path: Path) -> list[dict[str, Any]]:
  events: list[dict[str, Any]] = []
  for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
    if not line:
      continue
    try:
      value = json.loads(line)
    except json.JSONDecodeError as error:
      raise ValueError(f"{path}:{number}: invalid JSON: {error}") from error
    if not isinstance(value, dict):
      raise ValueError(f"{path}:{number}: event is not an object")
    events.append(value)
  return events


def summary(path: Path) -> dict[str, Any]:
  events = load_events(path)
  start = next((event for event in events if event.get("event") == "session_start"), {})
  end = next((event for event in reversed(events) if event.get("event") == "session_end"), {})
  measurements = [event for event in events if event.get("event") == "measurement"]
  records = [event for event in measurements if "sequence" in event and "outcome" in event]
  if measurements and not records:
    # Old session files contain only live standard-HRS notifications. They are
    # useful historic notes, but cannot answer an outcome-based comparison.
    return {
        "file": str(path),
        "label": start.get("label"),
        "schema": "legacy_live_notifications",
        "elapsed_seconds": end.get("elapsed_seconds"),
        "measurement_notifications": len(measurements),
        "comparison_ready": False,
        "reason": "This predates the ELIXIR_HR_STUDY outcome schema.",
    }
  for record in records:
    missing = {"sequence", "watch_tick", "outcome", "bpm"}.difference(record)
    if missing:
      raise ValueError(f"{path}: malformed study measurement missing {', '.join(sorted(missing))}")
  records.sort(key=lambda event: int(event["sequence"]))
  outcomes = Counter(str(event.get("outcome", "unknown")) for event in records)
  accepted = [event for event in records if event.get("outcome") == "accepted"]
  sequence_gaps = sum(max(0, int(right["sequence"]) - int(left["sequence"]) - 1) for left, right in zip(records, records[1:]))
  first_accepted_seconds: float | None = None
  if accepted and start.get("host_time_utc") and accepted[0].get("arrival_utc"):
    session_start = datetime.fromisoformat(str(start["host_time_utc"]))
    first_accepted = datetime.fromisoformat(str(accepted[0]["arrival_utc"]))
    first_accepted_seconds = round((first_accepted - session_start).total_seconds(), 3)
  accepted_bpms = [int(event["bpm"]) for event in accepted]
  result: dict[str, Any] = {
      "file": str(path),
      "label": start.get("label"),
      "algorithm": start.get("algorithm"),
      "elapsed_seconds": end.get("elapsed_seconds"),
      "measurement_windows": len(records),
      "accepted_windows": len(accepted),
      "acceptance_rate": round(len(accepted) / len(records), 4) if records else None,
      "outcomes": dict(sorted(outcomes.items())),
      "first_accepted_seconds": first_accepted_seconds,
      "sequence_gaps": sequence_gaps,
      "recovered_after_disconnect": sum(bool(event.get("recovered_after_disconnect")) for event in records),
      "connection_losses": end.get("connection_losses", 0),
      "connection_attempts": end.get("connection_attempts", 0),
      "duplicate_records": end.get("duplicate_records", 0),
  }
  if accepted_bpms:
    result["accepted_bpm_mean"] = round(statistics.fmean(accepted_bpms), 2)
    result["accepted_bpm_stdev"] = round(statistics.pstdev(accepted_bpms), 2)
    result["accepted_bpm_min"] = min(accepted_bpms)
    result["accepted_bpm_max"] = max(accepted_bpms)
  return result


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("session", type=Path)
  args = parser.parse_args()
  try:
    print(json.dumps(summary(args.session), indent=2, sort_keys=True))
  except (OSError, ValueError) as error:
    print(error, file=sys.stderr)
    return 1
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
