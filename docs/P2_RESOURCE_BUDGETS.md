# P2 resource budgets

This file versions the quantitative assumptions implemented by Phase 2. It is
an implementation contract, not a substitute for target/HIL evidence.

## Firmware image and static regions

`config/resource-baselines.json` records canonical no-extra-flags baselines for
both targets. Every supported build emits `size.json` from the linker map and
checks image bytes, total linked bytes, DIRAM, flash code, and flash rodata.
Small reviewed growth allowances catch regressions without coupling unrelated
toolchain padding to an exact byte count; raising a baseline or allowance
requires explicit architecture and resource review.

## OTA NVS endurance

The resumable OTA journal alternates two NVS keys (`j0` and `j1`). SHA-256 is
updated incrementally for every received byte. A journal record snapshots that
hash state only after each 512 KiB of new data, so ordinary 64 KiB HTTP ranges
do not each cause an NVS write or a full-prefix rehash.

| Board | OTA slot | Maximum journal `putBytes` per attempt | Key removals on close |
|---|---:|---:|---:|
| n16r8 | 0x300000 (3 MiB) | 6 | up to 2 |
| n8r4 | 0x330000 (3.1875 MiB) | 7 | up to 2 |

The write count is `1 + floor((imageBytes - 1) / 512 KiB)`: one empty-session
record and checkpoints strictly before completion. `esp_ota_end()` validates
the completed image; completion is not journaled. Both keys are removed when a
session completes, expires, is discarded, or fails.

`OTA_SUPPORTED_MAX_SLOT_BYTES` and `OTA_JOURNAL_MAX_WRITES_PER_SESSION` encode
the upper bound. Host tests fail if a supported partition grows past the
versioned budget without an explicit review.

This is a per-attempt bound. Repeated operator-initiated failed attempts remain
unbounded over product life, so release qualification must retain NVS wear
levelling, capture the attempt count externally, and include interrupted OTA
in the combined soak. The firmware does not claim a service-life figure until
the flash vendor endurance, NVS partition geometry, and expected field update
rate are fixed for production hardware.

## NVS capacity

Both supported partition tables reserve `0x15000` bytes (84 KiB) for NVS. With
4 KiB pages this gives 21 pages. The capacity contract conservatively reserves
two whole pages for NVS housekeeping/compaction, leaving 2,394 32-byte entries
for application records.

The worst-case application set is budgeted by the NVS blob rule
`2 + ceil(bytes / 32)` per blob, plus namespace and scalar-key entries:

| Records retained together | Maximum entries |
|---|---:|
| Settings A/B (`2 × 2,616 B`) | 168 |
| Shot History A/B (`2 × sizeof(ShotLogStore)`) | 366 |
| Last Shot | 8 |
| BLE settings A/B | 6 |
| Recovery intent | 3 |
| OTA journal A/B | 24 |
| Reset history, active pointers, and namespaces | 32 |
| **Application total** | **607** |

The resulting conservative compaction margin is 1,787 entries (74.6%). Host
tests bind the large record sizes and this arithmetic to the 84 KiB layout.
Diagnostic status and debug exports publish the installed partition size,
layout match, NVS used/free/available/total entries, namespace count, failure
count, last failing subsystem/operation/error, and flash-I/O lock timeouts.

## Combined heap/timing soak

`scripts/p2_soak.py` captures one status JSON object per interval as JSONL and
emits a machine-readable `.summary.json`. Headers, including an optional WebUI
claim, are read only from `SHOTSTOPPER_SOAK_HEADERS`; they are never copied into
the evidence. Prefer the public diagnostic endpoint when enabled:

```sh
python3 scripts/p2_soak.py \
  --url http://shotstopper.local/api/v1/status/diagnostic \
  --output artifacts/p2/combined-8h.jsonl \
  --scenario combined-ble-wifi-webhook-ota-nvs \
  --duration 28800
```

During the window, exercise BLE scanning/link/notifications, Wi-Fi scan and
reconnection, Web UI polling, webhook delivery/failure, settings and shot-log
writes, and interrupted/resumed OTA at every checkpoint. The runner fails on
fetch errors, reboot/uptime regression, stale snapshots, deadline misses,
increased BLE allocation fallback/HCI drops, heap below the versioned limits,
stack below 384 words when exported, or a sustained largest-block loss over
16 KiB. Missing stack samples are reported as `null`; a release run must start
the task profiler or use debug-export evidence so stack qualification is not
omitted.

The diagnostic snapshot also carries webhook worker/client lifecycle and
before/after heap-by-capability samples for each send. The analyzer rejects
more than one worker creation during a capture, missing lifecycle counters, or
more than one live HTTP client. A disabled webhook keeps an already-created
worker idle; only service shutdown performs stop/ack/join and releases it.

Required release artifacts are the JSONL, summary, firmware build ID, board
revision, board architecture, power/RF setup and an operator timeline marking
each injected event. Run 8 h for scheduling qualification and 72 h for the
long-term memory gate. A passing host self-test only validates the analyzer:

```sh
python3 scripts/p2_soak.py --self-test
```
