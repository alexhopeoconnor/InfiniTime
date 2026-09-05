#!/usr/bin/env bash
set -euo pipefail

readonly image="elixir-hr-study:local"

[[ "$#" -eq 1 ]] || { echo "Usage: $0 <session.jsonl>" >&2; exit 2; }
session="$(realpath -- "$1")"
[[ -f "$session" ]] || { echo "No such session: $session" >&2; exit 1; }

docker image inspect "$image" >/dev/null 2>&1 || { echo "Build $image first with record.sh." >&2; exit 1; }
docker run --rm --network=none --read-only --cap-drop=ALL --security-opt=no-new-privileges \
  --user "$(id -u):$(id -g)" --tmpfs /tmp:rw,noexec,nosuid,nodev,size=16m \
  --mount type=bind,src="$(dirname -- "$session")",dst=/in,readonly \
  --entrypoint python "$image" /app/summarize.py "/in/$(basename -- "$session")"
