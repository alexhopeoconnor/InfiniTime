# Heart-rate baseline recorder

This is a temporary, local test instrument for ElixirTime. It records only
standard Bluetooth Heart Rate Measurement notifications from an already-paired
PineTime. It is not a companion service, does not listen on a network port,
and does not write to the watch.

The Python environment runs only inside Docker. The container uses the host's
BlueZ D-Bus socket to communicate with the already-paired watch; BlueZ retains
ownership of the physical Bluetooth adapter and pairing information.

## Safety boundary

Runtime is deliberately restricted to:

- `--network=none`;
- a read-only root filesystem and read-only D-Bus socket mount;
- no Linux capabilities and `no-new-privileges`;
- a temporary writable `/tmp`;
- one private output directory, `scripts/hr-study/data/`.

Docker's default AppArmor profile blocks subscriptions through a deliberately
mounted system D-Bus socket, so the recorder sets its container AppArmor
profile to `unconfined`. This is the minimum tested adjustment for BlueZ D-Bus
access; it does not add host networking, Linux capabilities, privileged mode,
raw-HCI access, or any additional filesystem mounts.

The recorder does not scan, pair, alter watch settings, set the clock, flash
firmware, or expose a network service. If the watch leaves range during a
bounded session, it logs the connection loss and retries the existing paired
BlueZ device every 15 seconds until the session ends. A reconnection resumes
future notifications only: InfiniTime's current standard heart-rate BLE
service has no timestamped history to backfill the missed interval. Do not run
it while another desktop BLE client is using the watch. Stop it when a session
ends so the phone can reconnect normally.

## Record a baseline

With the watch's auto-sampling interval set to 30 seconds and the phone not
using the watch:

```sh
./scripts/hr-study/record.sh --label baseline-001 --duration 720
```

The first invocation builds the image. The dependency download happens during
that explicit build only; the recorder's runtime network is disabled.

For a simple comparable session:

1. Start the command.
2. Do a normal lap outside for about four minutes.
3. Sit at your desk until the twelve-minute recording ends.
4. Summarise the resulting file:

   ```sh
   ./scripts/hr-study/summarize.sh scripts/hr-study/data/baseline-001-<UTC timestamp>.jsonl
   ```

The summary calls a period without notifications a *BLE event gap*, not a
measurement failure: the current firmware may suppress repeated notifications
when a newly accepted BPM equals the previous value.

Compare two or more sessions without host Python or a watch connection:

```sh
./scripts/hr-study/compare.sh \
  scripts/hr-study/data/baseline-001-<UTC timestamp>.jsonl \
  scripts/hr-study/data/baseline-002-<UTC timestamp>.jsonl
```

The comparison reports link coverage for reconnect-aware sessions. It refuses
to treat notification count as a PPG-quality result unless a session kept a
clean link.

## Diagnose a desktop link loss

If a nearby watch repeatedly disconnects, capture a short HCI trace before
changing BlueZ configuration, rebonding, or reflashing firmware:

```sh
./scripts/hr-study/diagnose-link.sh --label link-001 --duration 120
```

This uses the native host `btmon` tool and requires sudo; when launched in an
interactive terminal it prompts normally. It is not Python and is not run in
Docker. It records raw BLE traffic to ignored, mode-0600 files in
`data/diagnostics/`, which may contain packet data and the watch address. The
paired device is not scanned, paired, configured, or written to. Decode the
trace locally with:

```sh
btmon --read scripts/hr-study/data/diagnostics/link-001-<UTC timestamp>.snoop \
  | rg -C 8 'Disconnection Complete|Reason'
```

Run the same baseline twice on the current stable firmware only after desktop
link health is known, then repeat the unchanged procedure on a temporary PPGv2
research firmware.
