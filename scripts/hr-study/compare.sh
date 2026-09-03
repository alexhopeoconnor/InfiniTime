#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly IMAGE="elixir-hr-study:local"

if [[ $# -lt 2 ]]; then
  echo "Usage: compare.sh SESSION-1.jsonl SESSION-2.jsonl [SESSION-N.jsonl ...]" >&2
  exit 2
fi

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "Build the recorder first with record.sh." >&2
  exit 1
fi

input_dir="$(cd -- "$(dirname -- "$1")" && pwd)"
container_sessions=()
for session in "$@"; do
  [[ -f "$session" ]] || { echo "session does not exist: $session" >&2; exit 1; }
  session_dir="$(cd -- "$(dirname -- "$session")" && pwd)"
  [[ "$session_dir" == "$input_dir" ]] || { echo "all sessions must be in the same directory" >&2; exit 2; }
  container_sessions+=("/input/$(basename -- "$session")")
done

exec docker run --rm --init \
  --network=none \
  --read-only \
  --cap-drop=ALL \
  --security-opt=no-new-privileges \
  --user "$(id -u):$(id -g)" \
  --tmpfs /tmp:rw,noexec,nosuid,nodev,size=16m \
  --mount type=bind,src="$SCRIPT_DIR",dst=/study,readonly \
  --mount type=bind,src="$input_dir",dst=/input,readonly \
  --entrypoint python \
  "$IMAGE" /study/compare.py "${container_sessions[@]}"
