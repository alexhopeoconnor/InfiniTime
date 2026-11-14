#!/usr/bin/env bash
# Prepare one explicitly requested study and keep the host awake until it ends.
set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly repository_root="$(cd -- "$script_dir/../.." && pwd)"
readonly cache_dir="${ELIXIR_ITD_CACHE_DIR:-$repository_root/.cache/itd}"

# Help must work before a build, without Docker, file writes, or Bluetooth.
for argument in "$@"; do
  if [[ "$argument" == --help || "$argument" == -h ]]; then
    exec bash "$script_dir/record.sh" --help
  fi
done
(( $# )) || { bash "$script_dir/record.sh" --help; exit 2; }
command -v systemd-inhibit >/dev/null || { echo "Missing systemd-inhibit." >&2; exit 1; }
running_recorders="$(docker ps --quiet --filter ancestor=elixir-hr-study:local)" || {
  echo "Cannot check Docker for an existing study recorder." >&2; exit 1;
}
if [[ -n "$running_recorders" ]]; then
  echo "A study recorder is already running; finish that session first." >&2
  exit 1
fi

umask 077
mkdir -p -- "$cache_dir"
exec 8>"$cache_dir/record.lock"
flock --nonblock 8 || { echo "Another prepared study session is running." >&2; exit 1; }
bash "$script_dir/prepare-helper.sh"

# Keep this shell alive to hold the session lock while systemd runs the child.
systemd-inhibit --what=sleep --mode=block --who=PinetimeStudy \
  --why="Bounded PineTime heart-rate study and retained-data recovery" \
  env ELIXIR_ITD_RUN_DIR=/tmp/elixir-itd ELIXIR_ITD_CONFIG_DIR=/tmp/elixir-itd-config \
  bash "$script_dir/record.sh" "$@"
