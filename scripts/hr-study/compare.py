#!/usr/bin/env python3
"""Compare one or more temporary ElixirTime HR-study JSONL sessions."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

from summarize import summary


def value(item: object) -> str:
  return "-" if item is None else str(item)


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("sessions", nargs="+", type=Path)
  args = parser.parse_args()
  try:
    rows = [summary(path) for path in args.sessions]
  except (OSError, ValueError) as error:
    print(error, file=sys.stderr)
    return 1

  columns = (
      ("label", "label"),
      ("algorithm", "algorithm"),
      ("windows", "measurement_windows"),
      ("accepted", "accepted_windows"),
      ("rate", "acceptance_rate"),
      ("first ok s", "first_accepted_seconds"),
      ("bpm sd", "accepted_bpm_stdev"),
      ("gaps", "sequence_gaps"),
      ("recovered", "recovered_after_disconnect"),
      ("losses", "connection_losses"),
  )
  widths = [len(title) for title, _ in columns]
  rendered = [[value(row.get(key)) for _, key in columns] for row in rows]
  for row in rendered:
    for index, cell in enumerate(row):
      widths[index] = max(widths[index], len(cell))

  def format_row(row: list[str]) -> str:
    return "  ".join(cell.ljust(widths[index]) for index, cell in enumerate(row))

  print(format_row([title for title, _ in columns]))
  print(format_row(["-" * width for width in widths]))
  for row in rendered:
    print(format_row(row))
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
