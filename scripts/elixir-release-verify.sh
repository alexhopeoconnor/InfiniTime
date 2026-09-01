#!/usr/bin/env bash
# Verify the only archive that may be selected in Gadgetbridge for ElixirTime.
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

version="$(sed -nE 's/^project\(pinetime VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt)"
if [[ -z "$version" ]]; then
  echo "Unable to read the numeric project version from CMakeLists.txt." >&2
  exit 1
fi

archive="${1:-build/output/pinetime-mcuboot-app-dfu-${version}.zip}"
image="build/output/pinetime-mcuboot-app-image-${version}.bin"
map_file="build/src/pinetime-mcuboot-app-${version}.map"

for required in "$archive" "$image" "$map_file"; do
  if [[ ! -f "$required" ]]; then
    echo "Missing required build output: $required" >&2
    exit 1
  fi
done

expected_contents=$'manifest.json\npinetime-mcuboot-app-image-'"${version}"$'.bin\npinetime-mcuboot-app-image-'"${version}"$'.dat'
actual_contents="$(unzip -Z1 "$archive" | LC_ALL=C sort)"
if [[ "$actual_contents" != "$expected_contents" ]]; then
  echo "Refusing archive with unexpected DFU contents:" >&2
  printf '%s\n' "$actual_contents" >&2
  exit 1
fi

if ! unzip -p "$archive" manifest.json | grep -F '"application"' >/dev/null; then
  echo "Refusing archive without an application manifest." >&2
  exit 1
fi

if ! grep -Fq 'WatchFaceTerminal' "$map_file"; then
  echo "Terminal watch face is not linked into the application." >&2
  exit 1
fi

if ! strings -a "$image" | grep -F 'ElixirTime' >/dev/null; then
  echo "The application does not expose the ElixirTime Device Information marker." >&2
  exit 1
fi

for removed_symbol in 'WatchFaceDigital' 'SettingWatchFace' 'MusicService' 'NavigationService' 'SimpleWeatherService'; do
  if grep -Fq "$removed_symbol" "$map_file"; then
    echo "Removed feature is linked into the application: $removed_symbol" >&2
    exit 1
  fi
done

echo "ElixirTime application DFU: $archive"
echo "Source commit: $(git rev-parse --short=12 HEAD)"
echo "SHA-256: $(sha256sum "$archive" | awk '{print $1}')"
echo "Device Information: software=ElixirTime firmware=${version}"
echo "READY: select only this ZIP in Gadgetbridge; never select another build/output artifact."
