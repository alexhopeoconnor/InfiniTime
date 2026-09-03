#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly IMAGE="elixir-hr-study:local"

if [[ $# -ne 1 ]]; then
  echo "Usage: summarize.sh SESSION.jsonl" >&2
  exit 2
fi

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "Build the recorder first with record.sh." >&2
  exit 1
fi

session="$1"
[[ -f "$session" ]] || { echo "session does not exist: $session" >&2; exit 1; }

exec docker run --rm --init \
  --network=none \
  --read-only \
  --cap-drop=ALL \
  --security-opt=no-new-privileges \
  --user "$(id -u):$(id -g)" \
  --tmpfs /tmp:rw,noexec,nosuid,nodev,size=16m \
  --mount type=bind,src="$SCRIPT_DIR",dst=/study,readonly \
  --mount type=bind,src="$(cd -- "$(dirname -- "$session")" && pwd)",dst=/input,readonly \
  --entrypoint python \
  "$IMAGE" /study/summarize.py "/input/$(basename -- "$session")"
