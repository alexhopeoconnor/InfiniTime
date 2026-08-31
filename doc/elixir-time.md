# ElixirTime

ElixirTime is the deliberately small PineTime firmware on the
`firmware/elixir-time` branch. It is based on the upstream InfiniTime `1.16.1`
tag, with `upstream` kept as the official repository remote.

The device continues to advertise as `InfiniTime`. This is intentional: it
keeps established phone compatibility and avoids treating a cosmetic project
name as a new BLE product. The Terminal watch face identifies the customised
firmware as `elixir@time`.

## Product choices

ElixirTime builds these user-facing features:

- Terminal and Digital watch faces (Terminal first in the selector).
- Alarm, timer, stopwatch, steps, and the heart-rate screen.
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
- **stale** for the following 90 seconds, visibly marked with `~` and an age;
- **unavailable** after two minutes, or before the first valid reading.

This is a display/data-quality improvement, not a claim of clinical accuracy.
The sensor still needs a manually started initial measurement before upstream
background heart-rate scheduling can operate.

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

For a first installation, keep a known-good upstream `.zip` and use the
normal application DFU route. Do not send a custom build until all of these
are true:

1. The application and recovery targets build successfully.
2. The generated DFU archive has been inspected and its checksum recorded.
3. The phone/desktop DFU client can see the watch and has enough battery.
4. A recovery route is available if the app does not boot.

The included charging cable supplies power only; it is not a USB data or SWD
debug connection. Physical recovery/debugging needs suitable SWD pogo pins
and a compatible programmer; see [SWD](SWD.md).

## Desktop diagnostics and companion boundaries

The normal long-term companion is the phone, not this development computer.
Use a desktop BLE connection only for a short, explicit diagnostic or DFU
session, and disconnect it afterwards so the phone can reconnect.

[ITD](https://github.com/Elara6331/itd) and its `itctl` command are useful
Linux diagnostics: they can read battery, heart rate, steps, and motion, set
the clock, and stream heart-rate or step notifications. `itctl` is a client of
the long-running `itd` daemon, however, rather than a direct one-shot BLE
tool. ITD's defaults reconnect, set the clock, and send connection
notifications, and it also includes services outside ElixirTime's scope.

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

ITD currently has no general switch to turn off its desktop-notification relay
or music initialisation. Do not run it on a desktop that may produce
notifications unless forwarding those notifications briefly is acceptable. A
warning about the missing Music service is expected: ElixirTime intentionally
omits Music, Navigation, and Weather and must not restore them just to silence
a companion warning.

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
state. Do not use ITD's firmware-upgrade, resource-loading, filesystem-write,
notification, or weather commands for ElixirTime maintenance.

For OTA, use the controlled legacy application-DFU route described above, not
`itctl firmware upgrade`. It must be run from an isolated Python environment
with BlueZ `gatttool`, `pexpect`, and `intelhex`, after a read-only connection
and service-discovery preflight. The DFU archive must be the exact checked
application artifact; wait for validation and activation/reset, then confirm
the running version. The checkout contains the legacy controller under
`bootloader/ota-dfu-python/`; the successful ElixirTime flash used a
disposable copy with longer connection/discovery timeouts, so revalidate the
tool before relying on it in a new host environment.

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

The companion InfiniSim build is also required after display changes. It
compiles the complete upstream source tree, which catches integration errors in
the surviving Terminal, Digital, and heart-rate screens even though production
ElixirTime deliberately builds a smaller target.

To bring in a later upstream release, create a temporary maintenance branch,
merge or rebase onto the desired upstream tag, build both targets, inspect the
feature and settings changes, then merge the tested result back into
`firmware/elixir-time`. Keep ElixirTime-specific changes small, concentrated
in registries and clearly commented patches; do not edit generated files.
