#!/usr/bin/env python3
"""Summarise one temporary ElixirTime HR-study JSONL session."""

from __future__ import annotations

import argparse
from collections import Counter
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
  # Arrival time is a transport timestamp, not a measurement timestamp.  A
  # recovered FIFO can be delivered minutes or hours after it was measured, so
  # comparing it to session_start would make cached sessions appear to acquire
  # a pulse late.  Watch ticks are captured with the measurement itself.  The
  # logger has no tick at START, so report the honest, comparable interval from
  # the first completed window rather than inventing a session-start offset.
  first_accepted_after_first_record_seconds: float | None = None
  tick_hz = start.get("watch_tick_hz")
  if accepted and records and isinstance(tick_hz, int) and tick_hz > 0:
    first_tick = int(records[0]["watch_tick"])
    first_accepted_after_first_record_seconds = round((int(accepted[0]["watch_tick"]) - first_tick) / tick_hz, 3)
  accepted_bpms = [int(event["bpm"]) for event in accepted]
  acknowledged_sequences = {
      int(event["sequence"])
      for event in events
      if event.get("event") == "record_acknowledged" and "sequence" in event
  }
  record_sequences = {int(record["sequence"]) for record in records}
  record_profiles = {int(record["algorithm_profile"]) for record in records if "algorithm_profile" in record}
  expected_profile = {"baseline": 0, "ppgv2": 1}.get(start.get("algorithm"))
  result: dict[str, Any] = {
      "file": str(path),
      "label": start.get("label"),
      "algorithm": start.get("algorithm"),
      "elapsed_seconds": end.get("elapsed_seconds"),
      "measurement_windows": len(records),
      "accepted_windows": len(accepted),
      "acceptance_rate": round(len(accepted) / len(records), 4) if records else None,
      "outcomes": dict(sorted(outcomes.items())),
      "first_accepted_after_first_record_seconds": first_accepted_after_first_record_seconds,
      "sequence_gaps": sequence_gaps,
      "recovered_after_disconnect": sum(bool(event.get("recovered_after_disconnect")) for event in records),
      "connection_losses": end.get("connection_losses", 0),
      "connection_attempts": end.get("connection_attempts", 0),
      "duplicate_records": end.get("duplicate_records", 0),
      "algorithm_profiles": sorted(record_profiles),
      # The host can prove it durably wrote every JSONL record. It cannot
      # always prove that it received an ATT success response: a link may drop
      # after the watch processed ACK(sequence). Name that observation honestly
      # instead of claiming the record is absent from the result set.
      "algorithm_profile_matches_requested": None
      if expected_profile is None or not record_profiles
      else record_profiles == {expected_profile},
  }
  if int(start.get("schema_version", 0)) >= 2:
    result["acknowledged_records"] = len(acknowledged_sequences.intersection(record_sequences))
    result["host_ack_unconfirmed_records"] = len(record_sequences.difference(acknowledged_sequences))
    result["ack_write_errors"] = sum(event.get("event") == "record_ack_error" for event in events)
  if "return_grace_seconds" in start or "collection_complete" in end:
    result["target_duration_seconds"] = end.get("target_duration_seconds", start.get("duration_seconds"))
    result["return_grace_seconds"] = end.get("return_grace_seconds", start.get("return_grace_seconds"))
    result["collection_complete"] = end.get("collection_complete")
    result["recovery_grace_expired"] = end.get("recovery_grace_expired")
    drain = next((event for event in reversed(events) if event.get("event") == "soft_end_drain"), None)
    result["soft_end_drain_complete"] = None if drain is None else drain.get("drained")
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
