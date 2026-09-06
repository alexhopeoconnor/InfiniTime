# Temporary heart-rate study recorder

This directory is a local comparison instrument, not a companion service. A
dedicated firmware build (`-DELIXIR_HR_STUDY=ON`) exposes a private,
unadvertised GATT service only to the already-paired desktop while a bounded
study is in progress. Normal ElixirTime builds do not contain this service.

The service has an encrypted control characteristic (`START`, `STOP`, `FLUSH`,
and a compact sequence `ACK`) plus a 20-byte indication-only record
characteristic. Each record represents one completed HR measurement window:
accepted BPM or a reason it failed, PPG summary, compact motion/step/ambient
context, and the watch tick. It is never raw PPG and is never a replacement
for the standard live Heart Rate Service.

The watch keeps 128 records in RAM. A BLE indication confirmation only proves
that BlueZ received a packet, so it does **not** discard the record. The
Docker logger first flushes the JSONL row, then writes `ACK(sequence)`; only
that matching acknowledgement removes the front record. On reconnect the
logger subscribes, then explicitly requests `FLUSH` once its callback and ACK
worker are ready. A retry may therefore repeat a sequence, which the logger
deduplicates while still acknowledging it. Amber `buf` means the watch is
collecting while the desktop is absent, blue `tx` / `ack` means an indication
or host acknowledgement is pending, and green `ok` means no record awaits
host acknowledgement. Power loss, a reboot, or more than 128 completed
windows discards pending RAM records.

## Safety and runtime boundary

Python runs only inside the image built from this directory. The recording
container has no network, no capabilities, a read-only root filesystem, a
small temporary filesystem, and only two host mounts: the read-only local
BlueZ D-Bus socket and the private output directory. It does not scan, pair,
listen on a port, change Bluetooth settings, or flash firmware. The container
uses `apparmor=unconfined` solely because Docker's default AppArmor profile
blocks BlueZ subscriptions over the explicitly mounted system D-Bus socket;
it does not add host networking, privileged mode, raw-HCI access, or Linux
capabilities.

The launcher performs one explicit `itctl set time now` through the reviewed
DFU-only ITD daemon, proves the connection with `firmware version`, and stops
that daemon before Docker connects. Use `--no-sync-time` only for a deliberately
non-mutating run. The launcher never starts a persistent service.

## Record and compare

With the phone Bluetooth off, the watch paired to this PC, and the study
firmware installed:

```sh
./scripts/hr-study/record.sh --label baseline-004 --algorithm baseline --duration 720
```

`--duration` is the collection target, not an appointment the wearer must
attend. If the watch is connected at that point, the logger drains queued ACKs,
sends `STOP`, and exits. If it is away, the default `--return-grace 900` keeps
the recorder retrying for another 15 minutes. On return it subscribes, sends
`FLUSH`, drains the retained FIFO, sends `STOP`, and exits. The watch must keep
collecting until it receives that stop command, so a delayed return can add
some post-target records; the JSONL records both the target and actual elapsed
duration. Use `--return-grace 0` only for diagnostics where abandoning an
unreconciled session is acceptable; normal profiles should retain the default.

For everyday profiles, use a 20- or 30-minute target, wear the watch normally,
walk around, then return and sit/type without watching the clock. At a
30-second background interval the 128-record RAM FIFO covers roughly 64
minutes, including a 12-minute target plus the default return grace. The
logger reconnects every 15 seconds using the existing BlueZ paired-device
object. Initial connection subscribes then sends `START`; a reconnect
subscribes, then explicitly sends `FLUSH`.

Each output is new, mode-0600 JSONL under ignored `scripts/hr-study/data/`.
Analyse without host Python or a BLE connection:

```sh
./scripts/hr-study/summarize.sh scripts/hr-study/data/baseline-004-<UTC>.jsonl
./scripts/hr-study/compare.sh scripts/hr-study/data/baseline-004-<UTC>.jsonl scripts/hr-study/data/ppgv2-005-<UTC>.jsonl
```

The comparison uses actual measurement-window outcomes, including unchanged
accepted BPMs. It reports acceptance rate, first accepted time, outcome mix,
accepted-BPM stability, sequence gaps, acknowledged/unacknowledged records,
reconnect losses, and records delivered after a reconnect. A record delivered
after reconnect is evidence of recovery, but cannot prove it was created while
disconnected; that distinction is not encoded into the compact watch record.
Treat a `collection_complete: false` session as transport diagnostics, not a
baseline profile.
