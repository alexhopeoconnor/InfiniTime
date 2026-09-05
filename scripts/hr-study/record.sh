#!/usr/bin/env bash
# Start one bounded ElixirTime HR-study recording session.
set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly repository_root="$(cd -- "$script_dir/../.." && pwd)"
readonly image="elixir-hr-study:local"
readonly default_address="CF:95:FC:A9:F2:B7"
readonly default_duration=720
readonly itd_run_dir="${ELIXIR_ITD_RUN_DIR:-/tmp/elixir-itd}"
readonly itd_config_dir="${ELIXIR_ITD_CONFIG_DIR:-/tmp/elixir-itd-config}"
readonly itd_bin="$itd_run_dir/itd"
readonly itctl_bin="$itd_run_dir/itctl"
readonly itd_socket="$itd_run_dir/itd.sock"
readonly itd_config_source="$repository_root/doc/itd-dfu-only.toml"

address="$default_address"
duration="$default_duration"
label=""
algorithm="baseline"
synchronize_time=true
itd_pid=""

usage() {
  cat <<'EOF'
Usage: record.sh --label NAME [--algorithm baseline|ppgv2] [--address BLE_ADDRESS]
                 [--duration SECONDS] [--no-sync-time]

Records one bounded temporary HR-study session from an already-paired PineTime.
It neither scans nor pairs and it never opens a network listener. By default it
performs one explicit clock sync through the restricted temporary itd daemon,
stops that daemon, then starts the Docker-only BLE logger.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --address)
      address="$2"
      shift 2
      ;;
    --duration)
      duration="$2"
      shift 2
      ;;
    --label)
      label="$2"
      shift 2
      ;;
    --algorithm)
      algorithm="$2"
      shift 2
      ;;
    --no-sync-time)
      synchronize_time=false
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
done

[[ "$duration" =~ ^[1-9][0-9]*$ ]] || { echo "duration must be a positive integer" >&2; exit 2; }
[[ "$label" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || { echo "label must use letters, numbers, dots, underscores, or hyphens" >&2; exit 2; }
[[ "$address" =~ ^([[:xdigit:]]{2}:){5}[[:xdigit:]]{2}$ ]] || { echo "address must be a Bluetooth MAC address" >&2; exit 2; }
[[ "$algorithm" == "baseline" || "$algorithm" == "ppgv2" ]] || { echo "algorithm must be baseline or ppgv2" >&2; exit 2; }

stop_time_sync_daemon() {
  if [[ -n "$itd_pid" ]] && kill -0 "$itd_pid" 2>/dev/null; then
    kill -INT "$itd_pid" 2>/dev/null || true
    wait "$itd_pid" 2>/dev/null || true
  fi
  itd_pid=""
  rm -f "$itd_socket"
}

synchronize_watch_time() {
  [[ -x "$itd_bin" && -x "$itctl_bin" ]] || {
    echo "Missing vetted itd/itctl binaries in $itd_run_dir. Rebuild them from public/embedded/itd as documented in doc/elixir-time.md." >&2
    exit 1
  }
  [[ -f "$itd_config_source" ]] || {
    echo "Missing DFU-only itd configuration: $itd_config_source" >&2
    exit 1
  }

  mkdir -p "$itd_run_dir" "$itd_config_dir/itd"
  cp "$itd_config_source" "$itd_config_dir/itd/itd.toml"
  rm -f "$itd_socket"

  echo "Synchronising PineTime clock, then releasing itd before the recorder connects."
  ELIXIR_DFU_ONLY=1 XDG_CONFIG_HOME="$itd_config_dir" "$itd_bin" >"$itd_run_dir/hr-study-itd.log" 2>&1 &
  itd_pid=$!

  for _ in {1..60}; do
    if [[ -S "$itd_socket" ]] && XDG_CONFIG_HOME="$itd_config_dir" "$itctl_bin" --socket-path "$itd_socket" firmware version; then
      XDG_CONFIG_HOME="$itd_config_dir" "$itctl_bin" --socket-path "$itd_socket" set time now
      stop_time_sync_daemon
      return
    fi
    if ! kill -0 "$itd_pid" 2>/dev/null; then
      echo "DFU-only itd stopped before the PineTime connected; see $itd_run_dir/hr-study-itd.log" >&2
      exit 1
    fi
    sleep 0.5
  done

  echo "Timed out waiting for the paired PineTime; see $itd_run_dir/hr-study-itd.log" >&2
  exit 1
}

trap stop_time_sync_daemon EXIT INT TERM

if [[ "$synchronize_time" == true ]]; then
  synchronize_watch_time
fi

if ! docker image inspect "$image" >/dev/null 2>&1; then
  echo "Building $image. Dependencies download only during this explicit image build; runtime networking is disabled."
  docker build --tag "$image" "$script_dir"
fi

umask 077
data_dir="$script_dir/data"
mkdir -p "$data_dir"
output="$data_dir/${label}-$(date -u +%Y%m%dT%H%M%SZ).jsonl"

docker run --rm --init \
  --network=none \
  --read-only \
  --cap-drop=ALL \
  --security-opt=no-new-privileges \
  --security-opt=apparmor=unconfined \
  --user "$(id -u):$(id -g)" \
  --tmpfs /tmp:rw,noexec,nosuid,nodev,size=16m \
  --mount type=bind,src=/run/dbus/system_bus_socket,dst=/run/dbus/system_bus_socket,readonly \
  --mount type=bind,src="$data_dir",dst=/out \
  "$image" record \
  --address "$address" \
  --duration "$duration" \
  --out "/out/$(basename -- "$output")" \
  --label "$label" \
  --algorithm "$algorithm"

echo "Study data: $output"
