#!/usr/bin/env bash
set -euo pipefail

readonly image="elixir-hr-study:local"

[[ "$#" -ge 2 ]] || { echo "Usage: $0 <session.jsonl> <session.jsonl> [...]" >&2; exit 2; }
docker image inspect "$image" >/dev/null 2>&1 || { echo "Build $image first with record.sh." >&2; exit 1; }

mounts=()
arguments=()
index=0
for input in "$@"; do
  session="$(realpath -- "$input")"
  [[ -f "$session" ]] || { echo "No such session: $session" >&2; exit 1; }
  target="/in/session-${index}.jsonl"
  mounts+=(--mount "type=bind,src=$session,dst=$target,readonly")
  arguments+=("$target")
  index=$((index + 1))
done

docker run --rm --network=none --read-only --cap-drop=ALL --security-opt=no-new-privileges \
  --user "$(id -u):$(id -g)" --tmpfs /tmp:rw,noexec,nosuid,nodev,size=16m \
  "${mounts[@]}" --entrypoint python "$image" /app/compare.py "${arguments[@]}"
