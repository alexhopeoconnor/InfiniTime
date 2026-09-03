#!/usr/bin/env bash
# Verify the only archive that may be sent by an ElixirTime DFU client.
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"

if [[ "$#" -ne 1 ]]; then
  echo "Usage: $0 <application-dfu-archive.zip>" >&2
  exit 2
fi

if [[ "$1" = /* ]]; then
  archive="$1"
else
  archive="$repo_root/$1"
fi

cd "$repo_root"

version="$(sed -nE 's/^project\(pinetime VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt)"
if [[ -z "$version" ]]; then
  echo "Unable to read the numeric project version from CMakeLists.txt." >&2
  exit 1
fi

archive_dir="$(dirname "$archive")"
image="${archive_dir}/pinetime-mcuboot-app-image-${version}.bin"
map_file="${archive_dir}/pinetime-mcuboot-app-${version}.map"

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

archive_image_sha256="$(unzip -p "$archive" "pinetime-mcuboot-app-image-${version}.bin" | sha256sum | awk '{print $1}')"
image_sha256="$(sha256sum "$image" | awk '{print $1}')"
if [[ "$archive_image_sha256" != "$image_sha256" ]]; then
  echo "Refusing archive whose application binary does not match the verified build image." >&2
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

for removed_symbol in 'WatchFaceDigital' 'SettingWatchFace' 'Screens::HeartRate' 'MusicService' 'NavigationService' 'SimpleWeatherService'; do
  if grep -Fq "$removed_symbol" "$map_file"; then
    echo "Removed feature is linked into the application: $removed_symbol" >&2
    exit 1
  fi
done

source_revision="$(git rev-parse --short=12 HEAD)"
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  source_revision="${source_revision} (working tree has changes)"
fi

echo "ElixirTime application DFU: $archive"
echo "Source revision: ${source_revision}"
echo "SHA-256: $(sha256sum "$archive" | awk '{print $1}')"
echo "Device Information: software=ElixirTime firmware=${version}"
echo "READY: send only this ZIP with the approved application-DFU workflow; never select another build/output artifact."
