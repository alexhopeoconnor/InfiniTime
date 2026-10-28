#!/usr/bin/env bash
# Recover one retained, completed ElixirTime HR-study FIFO without starting a new capture.
set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly image="elixir-hr-study:local"
readonly default_address="CF:95:FC:A9:F2:B7"
readonly default_return_grace=300

address="$default_address"
return_grace="$default_return_grace"
label=""
algorithm="ppgv2"

usage() {
  cat <<'EOF'
Usage: recover.sh --label NAME [--algorithm baseline|ppgv2] [--address BLE_ADDRESS]
                  [--return-grace SECONDS]

Subscribes and sends only the temporary study service's FLUSH command. It
durably records and acknowledges retained summary/diagnostic pairs, then sends
STOP once the FIFO drains. It never sends START, so it cannot overwrite an
out-of-range study whose recorder return grace expired.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --address)
      address="$2"
      shift 2
      ;;
    --return-grace)
      return_grace="$2"
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

[[ "$return_grace" =~ ^[1-9][0-9]*$ ]] || { echo "return-grace must be positive" >&2; exit 2; }
[[ "$label" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || { echo "label must use letters, numbers, dots, underscores, or hyphens" >&2; exit 2; }
[[ "$address" =~ ^([[:xdigit:]]{2}:){5}[[:xdigit:]]{2}$ ]] || { echo "address must be a Bluetooth MAC address" >&2; exit 2; }
[[ "$algorithm" == "baseline" || "$algorithm" == "ppgv2" ]] || { echo "algorithm must be baseline or ppgv2" >&2; exit 2; }

if ! docker image inspect "$image" >/dev/null 2>&1; then
  printf 'Missing %s; build it without starting a recording:\n  docker build --tag %q %q\n' "$image" "$image" "$script_dir" >&2
  exit 1
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
  --mount type=bind,src="$script_dir/ble_hr_logger.py",dst=/app/ble_hr_logger.py,readonly \
  "$image" recover \
  --address "$address" \
  --return-grace "$return_grace" \
  --out "/out/$(basename -- "$output")" \
  --label "$label" \
  --algorithm "$algorithm"

echo "Recovered study data: $output"
