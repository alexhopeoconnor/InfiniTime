#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly DEFAULT_ADDRESS="CF:95:FC:A9:F2:B7"
readonly DEFAULT_DURATION=120

address="$DEFAULT_ADDRESS"
duration="$DEFAULT_DURATION"
label=""
monitor_pid=""
monitor_wait_pid=""

usage() {
  cat <<'EOF'
Usage: diagnose-link.sh --label NAME [--address BLE_ADDRESS] [--duration SECONDS]

Captures a bounded host HCI trace with btmon while the Docker recorder observes
standard BLE Heart Rate Measurement notifications. The trace stays in ignored
scripts/hr-study/data/diagnostics/ and may contain BLE packet data.
EOF
}

cleanup() {
  if [[ -n "$monitor_pid" ]] && sudo -n kill -0 "$monitor_pid" 2>/dev/null; then
    sudo -n kill -INT "$monitor_pid" 2>/dev/null || true
  fi
  if [[ -n "$monitor_wait_pid" ]]; then
    wait "$monitor_wait_pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

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
command -v btmon >/dev/null || { echo "btmon is required; install the distribution BlueZ tools package first." >&2; exit 1; }
if [[ -t 0 && -t 1 ]]; then
  sudo -v
else
  sudo -n true 2>/dev/null || { echo "This bounded HCI trace needs an active sudo credential. Run it in a terminal so sudo can prompt, then retry." >&2; exit 1; }
fi

umask 077
trace_dir="$SCRIPT_DIR/data/diagnostics"
mkdir -p "$trace_dir"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
trace="$trace_dir/${label}-${timestamp}.snoop"
monitor_log="$trace_dir/${label}-${timestamp}.btmon.log"
monitor_pid_file="$trace_dir/${label}-${timestamp}.btmon.pid"
install -m 600 /dev/null "$trace"
install -m 600 /dev/null "$monitor_log"
install -m 600 /dev/null "$monitor_pid_file"

sudo -n sh -c 'echo "$$" > "$1"; exec btmon --write "$2"' sh "$monitor_pid_file" "$trace" >"$monitor_log" 2>&1 &
monitor_wait_pid="$!"
sleep 1
monitor_pid="$(<"$monitor_pid_file")"
if ! sudo -n kill -0 "$monitor_pid" 2>/dev/null; then
  wait "$monitor_wait_pid" || true
  echo "btmon did not start; see $monitor_log" >&2
  exit 1
fi

echo "HCI trace: $trace"
echo "Recorder runs for ${duration}s and disconnects at the end."
"$SCRIPT_DIR/record.sh" --label "$label" --address "$address" --duration "$duration"
