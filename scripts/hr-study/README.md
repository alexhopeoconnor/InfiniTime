# Temporary heart-rate study recorder

This directory is a local comparison instrument, not a companion service. A
dedicated firmware build (`-DELIXIR_HR_STUDY=ON`) exposes a private,
unadvertised GATT service only to the already-paired desktop while a bounded
study is in progress. Normal ElixirTime builds do not contain this service.

The service has an encrypted `START` / `STOP` control characteristic and a
20-byte indication-only record characteristic. Each record represents one
completed HR measurement window: accepted BPM or a reason it failed, PPG
summary, compact motion/step/ambient context, and the watch tick. It is never
raw PPG and is never a replacement for the standard live Heart Rate Service.

The watch keeps 128 records in RAM. It removes a record only after the normal
BLE indication confirmation arrives. If the desktop goes out of range, the
watch changes its Terminal Bluetooth row to amber `buf`, keeps recording, and
flushes on the next subscription. Blue `tx` means an indication is in flight;
green `ok` means the most recent record was confirmed. Power loss, a reboot,
or more than 128 completed windows discards unflushed RAM records.

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

Wear it normally, walk far enough away to lose the PC, return, then sit/type
until the 12-minute session ends. The logger reconnects every 15 seconds using
the existing BlueZ paired-device object. Initial connection subscribes then
sends `START`; reconnects only resubscribe, allowing the watch to drain its
own cache. Normal completion sends `STOP` before disconnecting.

Each output is new, mode-0600 JSONL under ignored `scripts/hr-study/data/`.
Analyse without host Python or a BLE connection:

```sh
./scripts/hr-study/summarize.sh scripts/hr-study/data/baseline-004-<UTC>.jsonl
./scripts/hr-study/compare.sh scripts/hr-study/data/baseline-004-<UTC>.jsonl scripts/hr-study/data/ppgv2-005-<UTC>.jsonl
```

The comparison uses actual measurement-window outcomes, including unchanged
accepted BPMs. It reports acceptance rate, first accepted time, outcome mix,
accepted-BPM stability, sequence gaps, reconnect losses, and records delivered
after a reconnect. A record delivered after reconnect is evidence of recovery,
but cannot prove it was created while disconnected; that distinction is not
encoded into the compact watch record.
