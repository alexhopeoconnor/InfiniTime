#!/usr/bin/env bash
# Restore the pinned helper from durable cache. Does not connect to Bluetooth.
set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly repository_root="$(cd -- "$script_dir/../.." && pwd)"
readonly cache_dir="${ELIXIR_ITD_CACHE_DIR:-$repository_root/.cache/itd}"
readonly run_dir=/tmp/elixir-itd
readonly config_dir=/tmp/elixir-itd-config

if [[ "${1:-}" == --help || "${1:-}" == -h ]]; then
  echo "Usage: prepare-helper.sh"
  echo "Verify the cached build and restore temporary helper files; does not connect."
  echo "ELIXIR_ITD_CACHE_DIR overrides the default repository .cache/itd directory."
  exit 0
fi
(( $# == 0 )) || { echo "Usage: prepare-helper.sh" >&2; exit 2; }
if pgrep -x itd >/dev/null; then
  echo "An itd process is already running; finish that session first." >&2
  exit 1
fi
build_dir="$(realpath -e -- "$cache_dir/current" 2>/dev/null)" || {
  echo "Missing helper build. Run scripts/itd/build.sh first." >&2; exit 1;
}
[[ -x "$build_dir/bin/itd" && -x "$build_dir/bin/itctl" && -f "$build_dir/SHA256SUMS" ]] || {
  echo "Incomplete helper build. Run scripts/itd/build.sh again." >&2; exit 1;
}
cmp --silent "$repository_root/scripts/itd/source.lock" "$build_dir/source.lock" || {
  echo "Cached helper inputs differ from this checkout; rebuild the helper." >&2; exit 1;
}
(
  cd -- "$build_dir"
  sha256sum --check --strict SHA256SUMS
)
readonly config_source="$repository_root/doc/itd-dfu-only.toml"
[[ -f "$config_source" ]] || { echo "Missing helper configuration." >&2; exit 1; }

umask 077
for directory in "$run_dir" "$config_dir" "$config_dir/itd"; do
  if [[ -L "$directory" || ( -e "$directory" && ( ! -d "$directory" || ! -O "$directory" ) ) ]]; then
    echo "Refusing an unexpected or unowned runtime directory: $directory" >&2
    exit 1
  fi
  install -d -m 700 -- "$directory"
done
install -m 700 -- "$build_dir/bin/itd" "$run_dir/itd"
install -m 700 -- "$build_dir/bin/itctl" "$run_dir/itctl"
install -m 600 -- "$config_source" "$config_dir/itd/itd.toml"
echo "Temporary DFU-only helper files ready."
