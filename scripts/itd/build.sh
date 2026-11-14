#!/usr/bin/env bash
# Build the pinned, patched helper without installing a companion service.
set -euo pipefail

readonly script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly repository_root="$(cd -- "$script_dir/../.." && pwd)"

usage() {
  cat <<'EOF'
Usage: build.sh [--source-repo PATH]

Build the reviewed itd and itctl into .cache/itd/current/bin.
--source-repo exports the pinned commit from an existing ordinary ITD repository
without changing it. Otherwise the pinned commit is fetched from upstream.
ELIXIR_ITD_CACHE_DIR overrides the cache directory. GOMODCACHE and GOCACHE may
reuse existing Go caches. Source/toolchain/module downloads occur only here,
never in prepare-helper.sh. No Bluetooth connection or service is started.
EOF
}

source_repo=""
while (( $# )); do
  case "$1" in
    --source-repo)
      (( $# >= 2 )) || { usage >&2; exit 2; }
      source_repo="$(realpath -e -- "$2")"
      shift 2
      ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done

for required in git go patch tar sha256sum flock; do
  command -v "$required" >/dev/null || { echo "Missing build tool: $required" >&2; exit 1; }
done
[[ "$(uname -s)" == Linux ]] || { echo "This helper recipe supports Linux hosts." >&2; exit 1; }

# This lock is reviewed source, not machine-local configuration.
source "$script_dir/source.lock"
readonly patch_file="$script_dir/elixir-dfu-only.patch"
[[ "$(sha256sum "$patch_file" | cut -d ' ' -f 1)" == "$ITD_PATCH_SHA256" ]] || {
  echo "ITD patch does not match source.lock." >&2; exit 1;
}

umask 077
cache_dir="${ELIXIR_ITD_CACHE_DIR:-$repository_root/.cache/itd}"
mkdir -p -- "$cache_dir/builds"
cache_dir="$(cd -- "$cache_dir" && pwd)"
exec 9>"$cache_dir/build.lock"
flock --nonblock 9 || { echo "Another helper build is running." >&2; exit 1; }
stage="$(mktemp -d "$cache_dir/builds/build.XXXXXXXX")"
next_link="$cache_dir/.current-$$"
cleanup() {
  [[ -z "$stage" ]] || rm -rf -- "$stage"
  rm -f -- "$next_link"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -- "$stage/source" "$stage/bin"

if [[ -n "$source_repo" ]]; then
  # No branch switch or worktree is needed; export the immutable commit.
  [[ "$(git -C "$source_repo" rev-parse "$ITD_UPSTREAM_REVISION^{commit}")" == "$ITD_UPSTREAM_REVISION" ]]
  git -C "$source_repo" archive --format=tar "$ITD_UPSTREAM_REVISION" >"$stage/source.tar"
else
  # A disposable bare object database is sufficient; no checkout is created.
  git init --bare --quiet "$stage/upstream.git"
  git --git-dir="$stage/upstream.git" fetch --quiet --no-tags --depth=1 "$ITD_UPSTREAM_URL" "$ITD_UPSTREAM_REVISION"
  [[ "$(git --git-dir="$stage/upstream.git" rev-parse 'FETCH_HEAD^{commit}')" == "$ITD_UPSTREAM_REVISION" ]]
  git --git-dir="$stage/upstream.git" archive --format=tar "$ITD_UPSTREAM_REVISION" >"$stage/source.tar"
  rm -rf -- "$stage/upstream.git"
fi
source_sha256="$(sha256sum "$stage/source.tar" | cut -d ' ' -f 1)"
tar -xf "$stage/source.tar" -C "$stage/source"
rm -- "$stage/source.tar"
patch --dry-run --batch --fuzz=0 -p1 -d "$stage/source" <"$patch_file"
patch --batch --fuzz=0 -p1 -d "$stage/source" <"$patch_file"
printf '%s\n' "$ITD_EMBEDDED_VERSION" >"$stage/source/version.txt"
cp -- "$script_dir/source.lock" "$stage/source.lock"

export GOTOOLCHAIN="go$ITD_GO_VERSION" GOWORK=off GOENV=off GOFLAGS=
export GOMODCACHE="${GOMODCACHE:-$cache_dir/go-modcache}"
export GOCACHE="${GOCACHE:-$cache_dir/go-buildcache}"
[[ "$(go env GOVERSION)" == "go$ITD_GO_VERSION" ]]
export GOOS=linux
GOARCH="$(go env GOHOSTARCH)"
export GOARCH
(
  cd -- "$stage/source"
  # Exported source has no Git identity. Record base+patch below instead of
  # claiming the original local customization commit as this build's revision.
  go build -mod=readonly -trimpath -buildvcs=false -o "$stage/bin/itd" .
  go build -mod=readonly -trimpath -buildvcs=false -o "$stage/bin/itctl" ./cmd/itctl
)
(
  cd -- "$stage"
  sha256sum bin/itd bin/itctl source.lock >SHA256SUMS
)
{
  printf 'upstream_url=%s\nupstream_revision=%s\nsource_archive_sha256=%s\npatch_sha256=%s\n' \
    "$ITD_UPSTREAM_URL" "$ITD_UPSTREAM_REVISION" "$source_sha256" "$ITD_PATCH_SHA256"
  printf 'embedded_version=%s\nbuilt_at_utc=%s\n' "$ITD_EMBEDDED_VERSION" "$(date -u +%FT%TZ)"
  go version
  go version -m "$stage/bin/itd" "$stage/bin/itctl"
} >"$stage/provenance.txt"

# Keep previous builds intact, and publish the pair with one atomic switch.
ln -s -- "builds/$(basename -- "$stage")" "$next_link"
mv -Tf -- "$next_link" "$cache_dir/current"
stage=""
echo "Verified helper build ready in $cache_dir/current/bin."
echo "Run scripts/hr-study/prepare-helper.sh to restore temporary runtime files."
