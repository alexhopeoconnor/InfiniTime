# ElixirTime agent guide

## Scope and priorities

Work on `firmware/elixir-time`, whose base is upstream InfiniTime `1.16.1`.
The current product priorities are, in order:

1. The visual design and usability of the Terminal/ElixirTime watch face.
2. Honest, understandable heart-rate behaviour on the existing HRS3300
   hardware.
3. Regression-free core watch features: time, alarms/timer/stopwatch, steps,
   notifications, and OTA recovery.

Do not start a Home Assistant bridge, persistent desktop companion, new BLE
protocol, or broad feature restoration unless the user explicitly asks. The
existing background heart-rate scheduler is intentionally auto-armed whenever
the user selects an interval; preserve its distinction from a manual
foreground measurement. The watch has no independent network link; future
remote integrations belong primarily on the phone side.

`doc/elixir-time.md` is the product and maintenance guide. Keep its statements
in sync when an ElixirTime-specific design decision changes.

## Device and Bluetooth safety

- The supplied cable is power-only. It is not a USB data, serial, or debug
  connection.
- Do not take over the watch's BLE connection while the phone or another
  companion is active without the user's confirmation. Stop the temporary
  desktop client when its task ends so the phone can reconnect.
- Never change, delete, or reinterpret the persisted `Settings` structure just
  because an ElixirTime feature is unbuilt. It remains compatibility data.

## ITD and itctl: optional diagnostics, never the flashing path

`itd` is a Linux daemon and `itctl` is its Unix-socket CLI. `itctl` is **not**
a direct BLE client: `itd` must be running and retains the active watch
connection for its lifetime.

Do not install, enable, or autostart `itd` merely to inspect the watch. In
particular, never run `systemctl --user enable itd` for this project. If the
user asks to use it, run it manually for a bounded diagnostic session with a
reviewed configuration, then stop it.

The upstream defaults are not read-only: they reconnect, set the watch time,
and send connection notifications. A diagnostic configuration must explicitly
turn off reconnection, connect/reconnect notifications, time setting, weather,
metrics, and FUSE:

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

Current ITD has no general switch to disable its desktop-notification relay or
music initialisation. Do not start it on a desktop that may emit notifications
unless forwarding them briefly is acceptable. ElixirTime intentionally does
not expose the Music, Navigation, or Weather services, so a missing-service
warning is expected and must not be "fixed" by re-adding them.

After that configuration is reviewed and the user has authorised the BLE
session, these are the permitted diagnostic operations:

```sh
itctl fw version
itctl get battery
itctl get heart
itctl get steps
itctl get motion
itctl watch heart --json
itctl watch steps --json
```

`itctl set time now` changes the watch clock; use it only when explicitly
requested. Do not use `itctl firmware upgrade`, `itctl resources`, mutating
`itctl filesystem` commands, `itctl notify`, or weather-update commands.

## Approved OTA update discipline

ITD is not the approved flasher. The known-good route is a normal,
application-only legacy BLE DFU using the generated
`build/output/pinetime-mcuboot-app-dfu-<version>.zip` archive and the Python
legacy controller in `bootloader/ota-dfu-python/`, run from an isolated virtual
environment with BlueZ `gatttool`, `pexpect`, and `intelhex` available.

The successful ElixirTime session used a disposable copy of that controller
with longer scan and GATT-discovery timeouts. Do not assume the vendored copy
or a global Python installation is ready: first perform a read-only
connect/discovery preflight against the current watch. Do not modify the
controller or flash merely to test it.

Before an OTA, all of the following are mandatory:

1. Build and validate both application and recovery targets, including
   InfiniSim after display changes.
2. Record and verify the SHA-256 of the exact application DFU archive.
3. Keep a known-good upstream application DFU archive available for rollback.
4. On the watch, enable `Settings -> Over-the-air -> Firmware & files`.
5. Confirm sufficient battery, an exclusive BLE connection, and a recovery
   route.
6. Read the DFU output through validation and activation/reset, then verify
   the running firmware before any further change.

Never flash a bootloader, recovery image, resource package, or arbitrary
archive as part of normal ElixirTime iteration. Physical recovery requires SWD
pogo pins and a compatible programmer.
