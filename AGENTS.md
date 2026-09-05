# ElixirTime agent guide

## Scope and priorities

Work on `firmware/elixir-time`, whose base is upstream InfiniTime `1.16.1`.
The current product priorities are, in order:

1. The visual design and usability of the Terminal watch face.
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

The user has explicitly authorised one narrow exception: temporary
`ELIXIR_HR_STUDY` comparison firmware may expose the private, unadvertised
Elixir HR Study GATT service. It is compiled out by default, captures one
20-byte completed-window summary at a time, and is only for a bounded Docker
recorder session with the phone Bluetooth off. Do not turn it into a companion
API, add it to advertising, replay cached data through the standard Heart Rate
Service, or ship it in normal ElixirTime builds.

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

## ITD and itctl: bounded desktop diagnostics and application DFU

`itd` is a Linux daemon and `itctl` is its Unix-socket CLI. `itctl` is **not**
a direct BLE client: `itd` must be running and retains the active watch
connection for its lifetime.

Do not install, enable, or autostart `itd` as a user service. In particular,
never run `systemctl --user enable itd` for this project. Run it manually for a
bounded diagnostic or application-DFU session, then stop it so the phone can
reconnect.

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

The vetted local ITD checkout is
`/home/alex/Documents/Projects/public/embedded/itd`, on branch
`elixir-dfu-only` (commit `922aa5a`, based on upstream `b79806e`). Its
`ELIXIR_DFU_ONLY=1` mode exposes only the temporary local control socket. It
does not initialise notification relay, music, calls, weather, metrics,
PureMaps, FUSE, clock setting, or reconnect behaviour. Use that mode for all
desktop sessions; do not replace it with upstream ITD defaults.

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
requested. The user has authorised `scripts/hr-study/record.sh` to perform one
such sync before a normal study session, unless its `--no-sync-time` flag is
used. It must first prove the connection with `itctl firmware version`, then
stop the temporary daemon before the Docker BLE recorder starts. Do not use
`itctl resources`, mutating `itctl filesystem` commands, `itctl notify`, or
weather-update commands.

## Approved desktop OTA update discipline

Use the vetted DFU-only `itd`/`itctl` path for normal, application-only desktop
OTA. Do not use the vendored Python legacy controller or a hand-modified copy
of it: its `gatttool` transport is not a reliable ElixirTime maintenance path
and can report a failed transfer poorly.

Before an OTA, all of the following are mandatory:

1. Build and validate both application and recovery targets. InfiniSim is not
   a required check: it is intentionally outside this project's toolchain.
2. Record and verify the SHA-256 of the exact application DFU archive.
3. Keep a known-good upstream application DFU archive available for rollback.
4. On the watch, enable `Settings -> Over-the-air -> Firmware & files`.
5. Confirm sufficient battery, an exclusive BLE connection, and a recovery
   route.
6. Start the temporary daemon with `ELIXIR_DFU_ONLY=1`, using a temporary
   `XDG_CONFIG_HOME` and socket, then prove the connection with `itctl firmware
   version`. The desktop must be paired with the watch; a stale bond that
   immediately disconnects must be removed and paired again, never worked
   around with the legacy script.
7. Run `scripts/elixir-release-verify.sh`, then send exactly the verified
   application archive with `itctl --socket-path /tmp/elixir-itd/itd.sock
   firmware upgrade --archive <absolute-application-zip>`. Keep the client in
   the foreground until it exits. This uses 20-byte Legacy-DFU packets and can
   take about nine minutes; do not start another BLE client or interrupt it.
8. After the expected reboot/disconnect, stop the daemon. Start a fresh
   DFU-only daemon and require `itctl firmware version` to report the newly
   built version before treating the transfer as successful.
9. On the watch, open `Settings -> Firmware` and validate only after the
   Terminal face and core smoke checks succeed.

Never flash a bootloader, recovery image, resource package, or arbitrary
archive as part of normal ElixirTime iteration. Physical recovery requires SWD
pogo pins and a compatible programmer.
