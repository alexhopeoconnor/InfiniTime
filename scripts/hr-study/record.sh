#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly IMAGE="elixir-hr-study:local"
readonly DEFAULT_ADDRESS="CF:95:FC:A9:F2:B7"
readonly DEFAULT_DURATION=720

address="$DEFAULT_ADDRESS"
duration="$DEFAULT_DURATION"
label=""

usage() {
  cat <<'EOF'
Usage: record.sh --label NAME [--address BLE_ADDRESS] [--duration SECONDS]

Records only standard BLE Heart Rate Measurement notifications through the
host's BlueZ D-Bus socket. It does not pair, scan, alter watch settings, or
open a network listener.
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

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "Building $IMAGE. Dependency downloads occur only during this build; runtime networking remains disabled."
  docker build --tag "$IMAGE" "$SCRIPT_DIR"
fi

umask 077
data_dir="$SCRIPT_DIR/data"
mkdir -p "$data_dir"
output="$data_dir/${label}-$(date -u +%Y%m%dT%H%M%SZ).jsonl"

exec docker run --rm --init \
  --network=none \
  --read-only \
  --cap-drop=ALL \
  --security-opt=no-new-privileges \
  --security-opt=apparmor=unconfined \
  --user "$(id -u):$(id -g)" \
  --tmpfs /tmp:rw,noexec,nosuid,nodev,size=16m \
  --mount type=bind,src=/run/dbus/system_bus_socket,dst=/run/dbus/system_bus_socket,readonly \
  --mount type=bind,src="$data_dir",dst=/out \
  "$IMAGE" record \
  --address "$address" \
  --duration "$duration" \
  --out "/out/$(basename -- "$output")" \
  --label "$label"
