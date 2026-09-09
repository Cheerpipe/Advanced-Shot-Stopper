# Resource budgets

This file versions the firmware's quantitative resource limits. The P2 filename
is retained for existing tool and evidence references. It is
an implementation contract, not a substitute for target/HIL evidence.

## Firmware image and static regions

`config/resource-baselines.json` records canonical no-extra-flags baselines for
both targets. Every supported build emits `size.json` from the linker map and
checks image bytes, total linked bytes, DIRAM, flash code, and flash rodata.
Small reviewed growth allowances catch regressions without coupling unrelated
toolchain padding to an exact byte count; raising a baseline or allowance
requires explicit architecture and resource review.
The current baselines were measured with ESP-IDF 6.1, its GCC 15.2 toolchain,
and the qualified `CONFIG_FREERTOS_IN_IRAM=y` build profile.

Both linker maps must also keep external BSS at or below 96 KiB and retain
`localBuzzer` and `taskProfiler` in internal DRAM. Moving their enclosing
objects to PSRAM would move synchronization state accessed under spinlocks.

## Runtime placement and allocation

| Resource | Placement and bound |
|---|---|
| Network work buffer | external, at most 64 KiB; mutually exclusive JSON-item and OTA-response scratch share storage under the work-buffer mutex |
| Profiler processing workspace | external, at most 4 KiB, only while running |
| Profiler kernel capture | internal, at most 4 KiB, only while running |
| Settings handoff | one 2620-byte external mailbox and one internal byte queued; no full settings copy in the queue or receiver |
| Web command | trivially copyable, at most 320 bytes; configuration and network payloads share a discriminated union |
| Radio settings snapshot | at most 192 bytes; full 2616-byte settings remain for durable mutations |
| Fixed buzzer melodies | at most 8 notes each; custom tune capacity remains 250 notes |
| JSON parser | PSRAM only; input at most 2047 bytes, nesting 32, values 128 |
| BBW adaptive candidates | control-owned fixed RAM, at most 3,000 bytes for eight presets; 20 observations and five trajectory anchors each |

Network command builders must activate their union member with
`setNetworkType()` before writing credentials. Preset metadata remains outside
the union because a preset operation also carries configuration. Persisted
record layouts are unchanged.

Settings V11/history V4 change byte meanings through explicit migration,
without growing either blob. Web gzip remains capped at 64,000 bytes combined:
500 bytes of the shell-JS allowance are reassigned to runtime (5,444 and 32,000
bytes respectively before the PM allocation below). Source authoring limits are
54,900 bytes HTML and 168,380 bytes JS, 223,280 combined: 1,000 more source bytes for selector readback/CSV
after condensing BBW help. These source allowances do not raise firmware or
combined compressed-asset limits.
Power management shares the Admin toggle persistence handler and adds 1,024
source bytes of allowance. It reallocates 400 compressed bytes from shell JS:
current limits are 5,044 shell JS, 32,200 runtime and 5,800 secondary views.
The combined 64,000-byte Web gzip cap and firmware/DRAM caps are unchanged.
The asynchronous configuration-save acknowledgement adds 256 source bytes of
allowance for revision/value readback and pending/failed persistence checks;
it does not raise compressed-asset limits.

Capability samples use `INTERNAL|8BIT` and `SPIRAM|8BIT`, including the PSRAM
minimum-free watermark. Diagnostic `memoryAllocations` reports cumulative
successes, failures, largest requested size, and last failed size by owner for
the application's capability-allocation wrappers. These counters are not live
allocation counts and do not include allocations made directly by SDK code.
The retained legacy external-fallback counter stays zero: there is no fallback.
JSON still allocates individual nodes, but those allocations no longer churn
the internal heap; an arena would require separate lifetime/concurrency evidence.
The compatibility field `jsonArenaExternal=false` means no arena is installed;
it does not describe the placement of the independently allocated documents.
The 4096-byte OTA transfer chunk remains request-scoped; retain it across
requests only if target traces justify the extra resident memory.

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
  --url http://192.168.1.50/api/v1/status/diagnostic \
  --output artifacts/p2/combined-8h.jsonl \
  --scenario combined-ble-wifi-webhook-ota-nvs \
  --duration 28800
```

During the window, exercise BLE scanning/link/notifications, Wi-Fi scan and
reconnection, Web UI polling, webhook delivery/failure, settings and shot-log
writes, and interrupted/resumed OTA at every checkpoint. The runner fails on
fetch errors, reboot/uptime regression, stale snapshots, deadline misses,
increased BLE allocation fallback/HCI drops, heap below the versioned limits,
stack below 1536 bytes, PSRAM free below 128 KiB or largest block below 64 KiB,
or a sustained internal largest-block loss over 16 KiB. Zero values are retained
and fail the limits; missing, invalid or unavailable required samples fail.
Control, scale-worker and BLE-host stack samples are required in every snapshot.
Use repeated `--require-task NAME` options for additional profiler tasks; each
requested task must appear in a running-profiler sample at every interval.
The free-margin threshold applies to those required tasks and the three
continuous health tasks. Other SDK tasks may have smaller configured stacks
(IDLE has only 1536 bytes total); their observed minimum is reported separately
as `profiledStackMinimumBytes`. Zero fails for every observed task. Qualifying
additional SDK tasks requires selecting them and reviewing their own margin.
The profiler stops after five minutes, so qualify those tasks in separate
bounded captures and retain their evidence with the long soak. A long capture
of the three always-exported stacks alone does not qualify every task.
`--min-stack-bytes` controls the stack gate; the deprecated `--min-stack-words`
alias also takes bytes for compatibility. Summary schema 2 uses explicit byte
names. Review allocation-failure counter deltas for every exercised owner.

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
