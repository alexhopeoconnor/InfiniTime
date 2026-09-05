# ElixirTime

ElixirTime is the deliberately small PineTime firmware on the
`firmware/elixir-time` branch. It is based on the upstream InfiniTime `1.16.1`
tag, with `upstream` kept as the official repository remote.

The device continues to use the established InfiniTime BLE device identity for
phone compatibility. Its standard Device Information **software revision** is
`ElixirTime`, while its firmware revision is `1.16.9`; the Terminal watch face
identifies the customised firmware as `elixir@time`. These independent,
visible identifiers make a successful custom flash distinguishable from an
upstream `1.16.1` image.

## Product choices

ElixirTime builds these user-facing features:

- The Terminal watch face only. Existing settings may name an upstream face,
  but the app safely falls back to Terminal without rewriting persisted data.
- Alarm, timer, stopwatch, steps, and background heart-rate sampling managed
  from Settings.
- Notifications, time synchronisation, standard OTA DFU, the file service,
  flashlight, battery information, firmware validation, and all core settings.

It does not build or register the Music, Navigation, or Simple Weather BLE
services. It also excludes Paint, Paddle, 2048, Dice, Metronome, Calculator,
the weather app and setting, the Motion app, and the unused upstream watch
faces. Their upstream source remains in the repository, unbuilt, so an
upstream merge stays reviewable.

Three null controller pointers remain in the display-controller aggregate only
because InfiniSim compiles all upstream screen sources. They are compatibility
hooks for those excluded traits, not live services or app registrations. A
simulator-only zero-sized weather-font stub lets those unreachable upstream
weather sources link without adding the weather font to the watch build.

`Settings` keeps the complete upstream persisted-settings layout, including
fields used by removed features. Do not delete or reorder those fields: the
layout is compatibility data, and unused values are harmless.

## Heart-rate behaviour

The HRS3300 supplies a best-effort PPG estimate. A zero from its algorithm is
now treated as an invalid estimate, not as a physiological zero. The most
recent verified reading is retained and classified as:

- **fresh** for 30 seconds;
- **stale** for the following 90 seconds, visibly marked with `~` and an age
  that advances once per second while the face is awake;
- **unavailable** after two minutes, or before the first valid reading.

This is a display/data-quality improvement, not a claim of clinical accuracy.
The PPG implementation already averages consecutive valid spectra. ElixirTime
arms a selected background interval immediately at boot or when it changes in
Settings; it no longer needs a manually started initial measurement. `Settings
-> Heart rate` is the sole control surface: choose a sampling interval, `Live`,
or `Off (hide)`. The latter stops automatic sampling and removes the heart-rate
row from the Terminal face. There is no foreground heart-rate app or manual
start action.

Heart-rate acquisition is suspended whenever external power is present. This
uses the cradle-presence signal rather than the transient charging-current
signal, so it remains disabled after the battery reaches 100%. Docking stops
an active acquisition immediately; undocking begins a fresh configured
interval. The Terminal heart row is icon-led: a green heart means a fresh
reading; amber means stale or a degraded signal; a slow amber/orange pulse
means a real optical acquisition is active; grey means unavailable; and a grey
heart followed by the plug glyph means the cradle has suspended measurement.
The pulse is deliberately an acquisition indicator, never a simulated
physiological heartbeat. The row preserves the latest valid value as `~BPM`
with its age, or uses short `...`, `?`, and `--` states rather than verbose
labels.

The screen reports the acquisition condition rather than calling every failure
"not enough data": it distinguishes initial acquisition, unstable optical
signal, excessive ambient light, and an HRS3300 communication failure. A
scheduled sample has a 30-second attempt window. Intervals longer than that
retain their start-to-start cadence; a failed 30-second sample waits for the
next interval instead of restarting continuously. `Live` deliberately keeps
sampling until it is changed to another option or Off. The firmware retains
only the latest verified value and its age; it does **not** yet maintain a
heart-rate history.

The Terminal face uses the built-in compact icon glyphs rather than long
`[LABEL]` prefixes. Its single top prompt and abbreviated status rows reserve
several display lines for future watch-local information. No Home Assistant
mode or multi-press button gesture is implemented yet: those need a defined
on-watch state model and remain outside the current firmware scope.

## Temporary HR comparison builds

`ELIXIR_HR_STUDY` is a disabled-by-default build option used only while
comparing the current estimator with the separate `research/ppgv2` branch. It
does not belong to normal ElixirTime firmware, does not change persisted
settings, and does not advertise a new service. A study build registers a
private, encrypted-control GATT service after the desktop connects. Its
20-byte indication record contains one completed measurement-window outcome:
accepted BPM or failure reason, PPG mean/range, compact accelerometer/step and
ambient context, power/mode flags, and a watch tick. It deliberately sends no
raw optical samples.

The HR task stores at most 128 such records in RAM. It retains the oldest one
until the paired desktop confirms its normal BLE indication; walking away from
the PC therefore buffers completed windows and returning resubscribes and
flushes the cache. The standard Bluetooth Heart Rate Service remains live-only
and never replays cached values as if they were current. A reboot, power loss,
or ring-buffer overflow loses pending study records by design.

During an active study the Terminal Bluetooth row makes this visible without
adding a new screen: amber `buf` means the watch is collecting while the
desktop is absent, blue `tx` means one indication is awaiting confirmation,
and green `ok` means the most recent record was confirmed. The code and its
Docker-only recorder are documented in
[`scripts/hr-study/README.md`](../scripts/hr-study/README.md). The launcher is
the sole approved automated time sync: it starts the restricted DFU-only ITD
daemon, runs one explicit `itctl set time now`, stops that daemon, and only
then starts the recorder. It can be made non-mutating with `--no-sync-time`.

## First upgrade and recovery checklist

The device inspected before this project was running InfiniTime `1.14.1`, but
its `/settings.dat` already used the `1.16.x` settings schema. A normal app
DFU to this branch should therefore preserve the existing configuration.

Before a *subsequent* OTA or file transfer on 1.16.x, open:

```
Settings -> Over-the-air -> Firmware & files -> Enabled
```

The inspected saved setting had this mode disabled. In 1.16.x that setting
also gates the DFU and file-system BLE services, so it must be enabled once
from the watch. Do not remove the OTA setting from ElixirTime.

InfiniTime's application clock is initialised from a build-time placeholder
after an application reboot; it needs an explicit BLE Current Time sync from a
phone companion or the bounded desktop tooling. The DFU-only desktop daemon
intentionally never syncs time as a side effect of a flash. The heart-rate
study launcher performs one explicit host-local time and timezone sync before
a normal test session, then stops that daemon before recording begins. This is
the appropriate place for that write; ordinary DFU and diagnostic sessions
must remain non-mutating.

For a first installation, keep a known-good upstream `.zip` and use the
normal application DFU route. Do not send a custom build until all of these
are true:

1. The application and recovery targets build successfully.
2. The generated DFU archive has been inspected and its checksum recorded.
3. The phone/desktop DFU client can see the watch and has enough battery.
4. A recovery route is available if the app does not boot.

The included charging cable supplies power only; it is not a USB data, serial,
or SWD debug connection. The daily firmware deliberately leaves the RTT log
backend disabled, so there is no cable-based serial diagnostic channel.
Physical recovery/debugging needs suitable SWD pogo pins and a compatible
programmer; [Pine64's devkit wiring guide](https://wiki.pine64.org/wiki/PineTime_Devkit_Wiring)
documents the GND, SWDIO, and SWDCLK connections and warns not to connect a
debugger supply while the battery is attached. A separate SWD/RTT debug build
is the appropriate future route for raw sensor logs; the on-watch acquisition
states above are the safe diagnostic aid in normal firmware. See [SWD](SWD.md).

## Desktop diagnostics and companion boundaries

The normal long-term companion is the phone, not this development computer.
Use a desktop BLE connection only for a short, explicit diagnostic or DFU
session, and disconnect it afterwards so the phone can reconnect.

[ITD](https://git.elara.ws/Elara6331/itd) and its `itctl` command are useful
Linux diagnostics: they can read battery, heart rate, steps, and motion, set
the clock, and stream heart-rate or step notifications. `itctl` is a client of
the long-running `itd` daemon, however, rather than a direct one-shot BLE
tool. ITD's defaults reconnect, set the clock, send connection notifications,
and initialise companion services outside ElixirTime's scope.

Do not enable ITD as a login service. If it is used for a debugging session,
start it manually with this conservative configuration, then stop it at the
end of the session:

```toml
[conn]
reconnect = false

[on.connect]
notify = false
setTime = false

[on.reconnect]
notify = false
setTime = false

[weather]
enabled = false

[metrics]
enabled = false

[fuse]
enabled = false
```

The local checkout at `/home/alex/Documents/Projects/public/embedded/itd`
contains a small reviewed branch, `elixir-dfu-only` at `922aa5a` (on top of
upstream `b79806e`). Its `ELIXIR_DFU_ONLY=1` mode deliberately starts only the
Bluetooth connection and local control socket. It does **not** relay desktop
notifications, initialise music/calls/weather/metrics/PureMaps/FUSE, set the
watch clock, or reconnect after a disconnect. Always use that mode for an
ElixirTime desktop session.

The temporary configuration is tracked as
[`itd-dfu-only.toml`](itd-dfu-only.toml). The compiled binaries and their
configuration belong in `/tmp`, not in a global package location or a user
service. If `/tmp/elixir-itd` has been cleared, rebuild from the reviewed
checkout (the Go module and build caches may also remain in `/tmp`):

```sh
itd_root=/home/alex/Documents/Projects/public/embedded/itd
cd "$itd_root"
git switch elixir-dfu-only
mkdir -p /tmp/elixir-itd
GOMODCACHE=/tmp/elixir-itd-go-modcache GOCACHE=/tmp/elixir-itd-go-buildcache go generate .
GOMODCACHE=/tmp/elixir-itd-go-modcache GOCACHE=/tmp/elixir-itd-go-buildcache go build -trimpath -buildvcs=true -o /tmp/elixir-itd/itd .
GOMODCACHE=/tmp/elixir-itd-go-modcache GOCACHE=/tmp/elixir-itd-go-buildcache go build -trimpath -buildvcs=true -o /tmp/elixir-itd/itctl ./cmd/itctl
```

The permitted inspection commands are:

```sh
itctl fw version
itctl get battery
itctl get heart
itctl get steps
itctl get motion
itctl watch heart --json
itctl watch steps --json
```

`itctl set time now` is safe when a time change is intended, but it changes
state. Do not use resource-loading, filesystem-write, notification, or weather
commands for ElixirTime maintenance.

For a desktop OTA, use only the temporary DFU-only ITD path—not the vendored
Python legacy controller. The latter uses an unreliable `gatttool` transport
and is retained only as upstream reference source, not as an ElixirTime tool.
The current known-good workflow is:

```sh
# On the watch first: Settings -> Over-the-air -> Firmware & files -> Enabled.
# Phone Bluetooth off; the PineTime is paired with this PC.

itd_root=/home/alex/Documents/Projects/public/embedded/itd
itd_run=/tmp/elixir-itd
itd_cfg=/tmp/elixir-itd-config
# Use the same build directory passed as BUILD_DIR to the container build.
version="$(sed -nE 's/^project\(pinetime VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt)"
build_dir="$PWD/build/ui-${version}"
archive="$build_dir/src/pinetime-mcuboot-app-dfu-${version}.zip"

mkdir -p "$itd_run" "$itd_cfg/itd"
cp doc/itd-dfu-only.toml "$itd_cfg/itd/itd.toml"

# In one terminal, foreground only; never enable a systemd service.
ELIXIR_DFU_ONLY=1 XDG_CONFIG_HOME="$itd_cfg" "$itd_run/itd"

# In a second terminal, first prove the live connection and package.
XDG_CONFIG_HOME="$itd_cfg" "$itd_run/itctl" --socket-path "$itd_run/itd.sock" firmware version
./scripts/elixir-release-verify.sh "$archive"

# Send only the application ZIP. Do not interrupt the roughly nine-minute transfer.
XDG_CONFIG_HOME="$itd_cfg" "$itd_run/itctl" --socket-path "$itd_run/itd.sock" \
  firmware upgrade --archive "$archive"
```

The archive must contain only `manifest.json`, the application `.bin`, and
the application `.dat`; it must never contain a bootloader, recovery image, or
resources. When the client exits, the watch should reboot. Stop the temporary
daemon, start a fresh DFU-only daemon, and require `firmware version` to report
the new version. Then check Terminal face/core behaviour on the watch and
validate it from `Settings -> Firmware`. The fresh daemon is necessary because
DFU-only mode intentionally does not reconnect after the reboot.

## Building and maintaining

Initial repository setup:

```sh
git switch firmware/elixir-time
git fetch upstream --tags
git log --oneline --decorate -1
```

Build with the documented toolchain/container workflow in
[Build with Docker](buildWithDocker.md), then build both the application and
recovery artifacts. The CI workflow is configured for pushes and pull requests
targeting this branch. On a fork whose default branch does not yet contain the
workflow, validate locally first: GitHub will not expose that branch-only
workflow for manual dispatch. The LittleFS target disables debug logging
because the pinned LittleFS debug trace has more formatting arguments than the
nRF5 SDK logger; this affects diagnostics only, not file-system behaviour.

InfiniSim is deliberately not part of the ElixirTime release process. It
requires additional desktop dependencies and offers primarily interactive UI
testing; it does not test the HRS3300, BLE radio, bootloader, or OTA transport.
The mandatory release checks are the reproducible application/recovery builds
and the deterministic package verification described in
[`scripts/elixir-release-verify.sh`](../scripts/elixir-release-verify.sh).

To bring in a later upstream release, create a temporary maintenance branch,
merge or rebase onto the desired upstream tag, build both targets, inspect the
feature and settings changes, then merge the tested result back into
`firmware/elixir-time`. Keep ElixirTime-specific changes small, concentrated
in registries and clearly commented patches; do not edit generated files.
