# Temporary heart-rate study recorder

This checkout's recorder matches the baseline's single-frame **protocol 3**.
It must not be used with the PPGv2 protocol 4 diagnostic firmware. The corrected
baseline build is `1.16.12`; label new captures `baseline-fixed-1.16.12-*` so
they remain distinguishable from the original `1.16.9` captures.

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
that daemon before Docker connects. Use `--no-sync-time` to skip the clock
write; the recorder still sends study control commands. The launcher never
starts a persistent service. DFU-only mode disables ITD's background companion
behavior but leaves its normal local control API available.

## Prepare the helper

Build the pinned helper once from the repository root:

```sh
./scripts/itd/build.sh
```

The [helper recipe](../itd/README.md) builds `itd` and `itctl` into ignored
`.cache/itd/`, with checksums and provenance. It can reuse an existing ordinary
source repository and Go caches; it does not need the sibling ITD checkout.
The build may download source, the pinned Go toolchain, and dependencies, but
does not connect to the watch.

Use `record-prepared.sh` for normal recording. It refuses an overlapping
recorder session, restores verified helper binaries and the tracked
configuration into `/tmp`, and prevents the PC from sleeping until the
bounded session exits. It does not rebuild missing binaries automatically.
Its `--help` path does not prepare files or access Bluetooth. To restore only
the helper after `/tmp` has been cleared, run:

```sh
./scripts/hr-study/prepare-helper.sh
```

Provide the already-paired watch's Bluetooth address using `--address` or the
`ELIXIR_HR_ADDRESS` environment variable. Set that value privately in your
shell; do not save real device addresses in tracked files. There is no
personal device address built into the launcher. The examples below assume
`ELIXIR_HR_ADDRESS` has been set.

## Record and compare

With the phone Bluetooth off, the watch paired to this PC, and the study
firmware installed:

```sh
./scripts/hr-study/record-prepared.sh --address "$ELIXIR_HR_ADDRESS" --label baseline-004 --algorithm baseline --duration 720
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
Keep manual counts and timing notes private alongside the recording. Do not
commit recordings, local readiness reports, cache manifests, or helper logs.
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
Treat a `collection_complete: false` session as transport diagnostics, not a
baseline profile.

## Manual pulse comparison

After flashing and checking the watch, select a 30-second HR interval. Wear
it normally, off its dock, sit quietly for five minutes, and remain near the
PC with phone Bluetooth off. Start a 15-minute session from this checkout:

```sh
./scripts/hr-study/record-prepared.sh --address "$ELIXIR_HR_ADDRESS" --label baseline-fixed-1.16.12-rest-001 --algorithm baseline --duration 900
```

Count for a full 60 seconds on a timed cue, for example during session minutes
1–2, 4–5, 7–8, 10–11, and 13–14, measured from the `study_started` event
(connection setup precedes it). Record each count's actual UTC start/end and
any movement or counting uncertainty. Avoid typing or touching the watch
during a count. Do not start the session until the wearer is ready.

Compare accepted completed windows individually with the manual count and
report availability separately. Failed windows are missing estimates, never
zero BPM; displayed stale values are not new measurements. The baseline uses
a nominal 6.4-second optical window and may average earlier spectra, whereas
the manual reference spans a minute, so start with steady seated measurements.
Prefer outcomes ending in the latter 30 seconds of a count, with a margin
for timing uncertainty, to keep the bounded acquisition attempt within the
reference interval.

Protocol 3 records watch uptime ticks at 1024 Hz, not wall-clock time. Clock
sync does not change those ticks. During an uninterrupted, promptly ACKed
connection, use live arrival timestamps to estimate the tick-to-UTC offset
and report its uncertainty. Exclude buffered/reconnected records from manual
time matching; their arrival times are not measurement times. Keep the raw
JSONL, manual counts, and timing assumptions together for later review.
