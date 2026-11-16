# Temporary ITD helper build

This directory preserves the ITD customization used for bounded ElixirTime
diagnostics and application DFU. The versioned inputs are the upstream source
lock, the small patch, and the build recipe. Generated source and binaries
stay in an ignored local cache. A sibling ITD checkout is optional; the
maintained recipe and patch in this repository are sufficient to rebuild.

## Source and customization

`source.lock` pins [upstream ITD](https://git.elara.ws/Elara6331/itd) at
`b79806e9de4ed1b9bd3af6bfb60db7c35dc49d35`, the patch checksum, the embedded
version string, and Go 1.23.8. `elixir-dfu-only.patch` preserves the existing
customization from local commit `922aa5a6e80c23cf105489e42c8f5a9adf2deb54`:

- `main.go`: `ELIXIR_DFU_ONLY=1` skips desktop notification relay, music,
  calls, weather, metrics, PureMaps, FUSE, automatic time setting, and
  connection notifications.
- `infinitime/infinitime.go`: lets DFU-only mode disable automatic reconnect.
- `infinitime/dfu.go`: bounds each awaited DFU response to 30 seconds and
  releases the notification subscription afterwards. This is not a timeout
  for the entire firmware upload.

DFU-only mode still exposes ITD's normal local control socket and API. It
limits background behavior, not the commands a local client can request.
Follow the permitted operations and OTA discipline in
[`doc/elixir-time.md`](../../doc/elixir-time.md).

ITD is developed upstream by Elara and contributors. Its source includes the
GNU GPL version 3 license, and the customized source files carry GPLv3-or-later
notices. The build retains upstream `LICENSE` and source notices in its cached
source. The patch does not replace those notices.

## Build

From the InfiniTime repository root:

```sh
./scripts/itd/build.sh
```

The script fetches the pinned revision into temporary source metadata and
exports it for the build. It does not create or use Git worktrees. To obtain
the same source from an existing ordinary ITD repository instead of fetching:

```sh
./scripts/itd/build.sh --source-repo /path/to/itd
```

The source repository must contain the pinned revision. The recipe exports
that revision without switching branches or using local uncommitted files.
Go dependencies and the pinned toolchain may still need downloading if they
are not cached. Existing `GOMODCACHE` and `GOCACHE` settings are respected.

The build verifies the patch, generates the required embedded `version.txt`,
and compiles only `.` and `./cmd/itctl` with read-only module resolution and
trimmed build paths. The generated RPC source is already present upstream;
there is no need for `protoc` or recursive `go generate`. Exported source uses
explicit provenance instead of claiming to be a clean checkout of the old
custom commit. Its binary hashes can differ from that historical build.

The default cache layout is:

```text
.cache/itd/
  builds/<build-id>/
    source/
    bin/itd
    bin/itctl
    SHA256SUMS
    provenance.txt
  current -> builds/<build-id>
  go-modcache/
  go-buildcache/
```

The build prepares a new directory and promotes `current` only after both
binaries and their checksums are ready. A failed rebuild leaves the previous
completed build available. `ELIXIR_ITD_CACHE_DIR` overrides the cache root;
use the same setting for build, preparation, and recording.

The default `.cache/` directory is already ignored. Keep any alternative cache
outside tracked source, and never force-add binaries, logs, or generated
provenance. Build provenance records source and toolchain details for local
verification; it is not a file to publish.

This cache is disposable build state. Keep retained firmware archives under
`$HOME/Documents/dev-share/pinetime/firmware/` and private study recordings
under `$HOME/Documents/dev-share/pinetime/study-sessions/` instead. The study
launcher accepts `ELIXIR_HR_DATA_DIR` for another private output destination.
Use the maintained launchers in this repository; standalone helper scripts
and recorder snapshots are no longer needed.

## Prepare and use

Restore the cached binaries and tracked configuration into `/tmp`:

```sh
./scripts/hr-study/prepare-helper.sh
```

Preparation verifies the cached checksums, refuses to replace an active ITD
helper, and creates temporary runtime files with private permissions. It does
not build, start a daemon, or connect to the watch. If `/tmp` is cleared, run
preparation again; the durable build cache does not need rebuilding.

For a study, use [`record-prepared.sh`](../hr-study/record-prepared.sh), which
prepares the helper and prevents sleep for the bounded session. Set the watch
address privately through `ELIXIR_HR_ADDRESS` or pass `--address`; see the
[study instructions](../hr-study/README.md) for baseline protocol compatibility.
The wrapper's `--help` path does not prepare runtime files or access Bluetooth.

For diagnostics or OTA, start the temporary helper manually with
`ELIXIR_DFU_ONLY=1` and the tracked configuration as documented in the product
guide. Stop it and release the Bluetooth connection when the task ends.
Neither script installs or enables an ITD service, and the helper should never
autostart at login.
