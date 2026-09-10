# Temporary heart-rate study recorder

This directory is a local comparison instrument, not a companion service. A
dedicated firmware build (`-DELIXIR_HR_STUDY=ON`) exposes a private,
unadvertised GATT service only to the already-paired desktop while a bounded
study is in progress. Normal ElixirTime builds do not contain this service.

The service has an encrypted control characteristic (`START`, `STOP`, `FLUSH`,
and a compact sequence `ACK`) plus two 20-byte indication-only characteristics.
Each completed HR measurement is a checksummed summary frame followed by its
checksummed diagnostic frame. Together they carry the outcome, PPG mean/range,
motion and HRS3300 drive/gain state; PPG analysis, strongest/locked frequency
bin and energy state; sensor transitions; and heart-rate task stack/queue and
heap headroom. It is never raw PPG and is never a replacement for the standard
live Heart Rate Service. The logger writes and `fsync`s both frames as one
measurement before acknowledging the sequence. A malformed, unknown, or
checksum-failed frame invalidates the session and is never acknowledged.

The watch keeps 128 summary/diagnostic pairs in RAM. A BLE indication confirmation only proves
that BlueZ received a packet, so it does **not** discard the record. The
Docker logger flushes and `fsync`s the JSONL row, then writes
`ACK(sequence)`; only
that matching acknowledgement removes the front record. On reconnect the
logger subscribes, then explicitly requests `FLUSH` once its callback and ACK
worker are ready. Its drain quiet period begins only after that `FLUSH` write
is accepted, so a delayed first retained indication cannot be mistaken for an
empty FIFO. If an ACK write fails, the logger does not send `STOP`; it
reconnects and retries the retained front record. A retry may therefore repeat
a sequence, which the logger
deduplicates while still acknowledging it. Amber `buf` means the watch is
collecting while the desktop is absent, blue `tx` / `ack` means an indication
or host acknowledgement is pending, and green `ok` means no record awaits
host acknowledgement. `START` carries a capture lease of at most 45 minutes.
When that lease expires the watch stops adding study records but retains its
FIFO for a later `FLUSH` and durable ACK; `STOP` then ends the temporary
transport. This makes a delayed return safe without pretending the RAM FIFO is
overnight storage. Power loss, a reboot, or more than 128 completed windows
before the lease is acknowledged discards pending RAM records.

## Safety and runtime boundary

Python runs only inside the image built from this directory. The recording
container has no network, no capabilities, a read-only root filesystem, a
small temporary filesystem, and only three host mounts: the read-only local
BlueZ D-Bus socket, the private output directory, and the read-only current
recorder script. The summary and comparison wrappers likewise mount their
current local analysis scripts read-only. This keeps execution inside Docker
while preventing an offline image from silently running stale reviewed code.
It does not scan, pair, listen on a port, change Bluetooth settings, or flash firmware. The container
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

`--duration` is the on-watch capture lease, not an appointment the wearer must
attend. It defaults to 12 minutes and is capped at 45 minutes because the
watch has only a 128-record RAM FIFO. If the watch is connected at the capture
deadline, the logger drains queued ACKs, sends `STOP`, and exits. If it is
away, the watch stops adding records at its own deadline and retains the
captured FIFO. The default `--return-grace 900` keeps the recorder retrying for
another 15 minutes; on return it subscribes, sends `FLUSH`, drains the retained
records, sends `STOP`, and exits. A longer return grace is safe when the
desktop remains awake: it waits for recovery but does not make the watch retain
more than the lease. Use `--return-grace 0` only for diagnostics where
abandoning an unreconciled session is acceptable; normal profiles should retain
the default.

For everyday profiles, use the default 12-minute lease, wear the watch
normally, walk around, then return and sit/type without watching the clock. At
a 30-second background interval the 128-record RAM FIFO covers roughly an
hour; the 45-minute maximum leaves a margin for variation in completed-window
cadence. The logger reconnects every 15 seconds using the existing BlueZ paired
device object. Initial connection subscribes then sends the duration-bearing
`START`; a reconnect subscribes, then explicitly sends `FLUSH`.

Each output is new, mode-0600 JSONL under ignored `scripts/hr-study/data/`.
Analyse without host Python or a BLE connection:

```sh
./scripts/hr-study/summarize.sh scripts/hr-study/data/baseline-004-<UTC>.jsonl
./scripts/hr-study/compare.sh scripts/hr-study/data/baseline-004-<UTC>.jsonl scripts/hr-study/data/ppgv2-005-<UTC>.jsonl
```

The comparison uses actual measurement-window outcomes, including unchanged
accepted BPMs. It reports acceptance rate, time from the first completed
window to the first accepted window (using watch ticks, so cached records do
not look artificially late), outcome mix, accepted-BPM stability, sequence gaps,
the recorded estimator profile, host-observed ACK confirmations, reconnect
losses, and records delivered after a reconnect. An `ack ?` result means the
host did not observe an ACK success response; it does **not** mean the JSONL
measurement was lost or that the watch necessarily retained it, because a BLE
link can drop after the watch processes the ACK. A record delivered after
reconnect is evidence of recovery, but cannot prove it was created while
disconnected; that distinction is not encoded into the compact watch record.
The comparison exits non-zero if a recorded estimator profile does not match
its requested `--algorithm`; that session has an invalid test identity and
must not be used as either baseline or PPGv2 evidence.
Treat a `collection_complete: false` session as transport diagnostics, not a
baseline profile.
