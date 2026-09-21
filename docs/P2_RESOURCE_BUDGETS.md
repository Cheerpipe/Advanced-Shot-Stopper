# Resource budgets

This file versions the firmware's quantitative resource limits. The P2 filename
is retained for existing tool and evidence references. It is
an implementation contract, not a substitute for target/HIL evidence.

## Firmware image and static regions

`config/resource-baselines.json` records the canonical baselines for both
targets. Every test or review that measures these budgets must compile with
both `--jtag` and `--development`; these options usually produce a larger
firmware image and therefore provide the conservative measurement. Comparisons
must use the same hardware and machine profiles and the same two options on
both sides. Every supported build emits `size.json` from the linker map and
checks image bytes, total linked bytes, DIRAM, flash code, and flash rodata.
Small reviewed growth allowances catch regressions without coupling unrelated
toolchain padding to an exact byte count; raising a baseline or allowance
requires explicit architecture and resource review.
The current baselines were measured with ESP-IDF 6.1, its GCC 15.2 toolchain,
and the qualified `CONFIG_FREERTOS_IN_IRAM=y` build profile.

The n16r8 baseline represents the largest reviewed supported profile, currently
the Linea Micra cloud build. HTTPS server verification adds the ESP certificate
bundle in flash; it is retained rather than weakening TLS. The development
profile with USB Serial/JTAG measures 2,063,520 image bytes and 2,063,403 total
bytes. The versioned allowances retain 38,640 and 38,628 bytes of reviewed
growth headroom respectively. Flash rodata is 506,660 bytes, flash code is
1,398,548 bytes, and linked DIRAM is 169,502 bytes; each retains its versioned
allowance. The 3 MiB OTA slot still has more than 1 MiB free.

Both linker maps must also keep external BSS at or below 105 KiB and retain
`localBuzzer` and `taskProfiler` in internal DRAM. Moving their enclosing
objects to PSRAM would move synchronization state accessed under spinlocks.
The extra 1 KiB ceiling covers the versioned Micra cloud account record while
the build remains below 104.4 KiB measured. This is static PSRAM, not internal
heap. The earlier 96→104 KiB raise covers the V3 half-second shot-curve store.

## Runtime placement and allocation

| Resource | Placement and bound |
|---|---|
| Network work buffer | external, at most 68 KiB; mutually exclusive JSON-item and OTA-response scratch share storage under the work-buffer mutex, and a one-curve JSON scratch serves the status and shots-list rows |
| Shot-curve store | external, 26,820 bytes for 100 V3 records; the Network work buffer may hold one separate 26,800-byte read copy within its 68 KiB total bound |
| Shared flash-I/O scratch | internal heap, 2,960 bytes (one PersistedSettings record); slots are read, written, and verified sequentially under the flash-I/O lock, with no PSRAM fallback; the larger partition stores transfer in 1 KiB chunks staged through the same scratch |
| USB serial output | internal heap, 2,064 bytes for the eight-record ESP log queue; one external 2,560-byte CLI reply buffer; startup failures free both allocations, and successful startup retains one boot-lifetime owner |
| Micra cloud workspace | external and lazy; exactly 4,120 bytes of bounded session/token state while cloud observation is active, plus one request-scoped 16 KiB buffer whose mutually exclusive request-body and response phases share storage; Disconnect, disabled observation, STA loss, and AP entry destroy the client and free both blocks |
| Micra/Webhook TLS allocations | external through the Micra profile's mbedTLS allocator; dynamic record, certificate, handshake, and session objects never fragment internal DRAM and are freed through the matching capability allocator |
| Profiler processing workspace | external, at most 4 KiB, only while running |
| Profiler kernel capture | internal, at most 4 KiB, only while running |
| Settings handoff | one 2,964-byte external mailbox and one internal byte queued; no full settings copy in the queue or receiver |
| Web command | trivially copyable, at most 328 bytes; configuration and network payloads share a discriminated union |
| Radio settings snapshot | at most 224 bytes; full 2,960-byte settings remain for durable mutations |
| Wi-Fi static TX pool | eight internal buffers reserved while Wi-Fi is initialized; sized for the bounded Web UI, OTA, webhook, STA, and SoftAP workload, with reliability taking priority over peak Web UI throughput |
| mDNS responder | NetworkService-owned; mDNS 1.13.1 places its 4096-byte priority-1 task stack on core 0 and dynamic responder allocations in PSRAM, while static synchronization/control storage stays internal; one persistent UDP socket (lwIP socket budget 8→10); always-on passive responder, never gated for shots/scale/AP/HTTP, freed once in `OpenBrewByWeightNetwork::stop()`; SDK heap allocations bypass application counters |
| Fixed buzzer melodies | at most 8 notes each; custom tune capacity remains 250 notes |
| JSON parser | PSRAM only; Web input remains at most 2047 bytes / 128 values; the Micra worker explicitly admits at most 16 KiB / 1024 values for bounded cloud responses; nesting remains 32 |
| BBW adaptive candidates | control-owned fixed RAM, at most 3,000 bytes for eight presets; 20 observations and five trajectory anchors each |

Network command builders must activate their union member with
`setNetworkType()` before writing credentials. Preset metadata remains outside
the union because a preset operation also carries configuration. Settings
schema 1 uses a 2,960-byte blob for the bounded Micra cloud account and selected
machine. Earlier settings schemas are rejected and require `--erase-all`.

History V5 retains an exact bounded preset-name snapshot and transfers through
the shared chunked flash-I/O path. The separate last-shot V4 record retains the
same provenance. The rendered English Web UI is capped at 69,100 bytes HTML,
193,300 bytes JavaScript, and 262,300 bytes combined authoring source. Compressed
limits are 36,900 bytes for runtime JavaScript and 107,200 bytes for all embedded
Web assets; the Micra cloud build measures 69,031 / 193,267 authoring bytes and
36,851 / 107,156 compressed bytes respectively.

Every new setting must include concise, natural help that explains its effect on
the barista's workflow, including what changes when an option is enabled or
disabled. Adding a setting is expected to increase the Web UI budget, and the
applicable source, compressed-asset, and firmware limits must be raised through
the normal measured review when necessary. Removing, shortening, or making help
less useful merely to fit an earlier budget is not acceptable: a clear,
friendly, well-constructed UI takes priority over preserving the previous Web UI
byte allowance. Per-asset caps remain independently enforced by Web contract
tests, so one asset cannot consume all combined headroom.

Both supported partition tables reserve a dedicated `shotcurve` data partition
at custom subtype `0x40`, exactly `0xE000` (56 KiB). It contains two
erase-aligned `0x7000` (28 KiB) slots, so the 26,820-byte store retains 1,852
bytes of per-slot headroom. On n8r4 it occupies `0x680000`–`0x68DFFF`; on n16r8
it occupies `0x620000`–`0x62DFFF`. The following filesystem region is reduced
without moving either OTA application or the coredump endpoint.

Capability samples use `INTERNAL|8BIT` and `SPIRAM|8BIT`, including the PSRAM
minimum-free watermark. Diagnostic `memoryAllocations` reports cumulative
successes, failures, largest requested size, and last failed size by owner for
the application's capability-allocation wrappers. These counters are not live
allocation counts and do not include allocations made directly by SDK code.
The retained legacy external-fallback counter stays zero: there is no fallback.
JSON and Micra-profile mbedTLS still allocate individual objects, but those
allocations no longer churn the internal heap; an arena would require separate
lifetime/concurrency evidence.
The Micra worker deletes each bounded cloud document before releasing its
request buffer, so those temporary PSRAM blocks can coalesce after each poll.
The compatibility field `jsonArenaExternal=false` means no arena is installed;
it does not describe the placement of the independently allocated documents.
The 4096-byte OTA transfer chunk remains request-scoped; retain it across
requests only if target traces justify the extra resident memory.

Internal-heap diagnostics also publish allocated, free, and total block counts.
`internalHeapFragmentationPermille` is
`1000 × (total free - largest free block) / total free`, guarded to zero when
total free is zero or the reported largest block covers it. ESP32-S3 internal
memory contains multiple allocator regions, so this ratio is a trend indicator,
not a claim that all free bytes can form one allocation. HTTP, Wi-Fi, OTA,
Micra TLS, and webhook TLS owners retain only a bounded last before/after sample,
signed deltas, cycle count, stale-start count, worst free/largest loss, and
maximum free-block increase. Sampling occurs outside owner locks and outside
OTA chunk/cache-off work; only the fixed result is copied under the existing
owner mutex.

The n16r8 Micra development+JTAG candidate measured 169,502 linked DIRAM bytes.
The one-record scratch removes 2,960 bytes from its lazy runtime allocation,
while the disabled-USB path avoids the 2,064-byte serial payload. Their actual
free/largest-block effects remain target measurements, not linked-memory claims.

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
| Settings A/B (`2 × 2,748 B`) | 176 |
| Last Shot (`2 + ceil(252 / 32)`) | 10 |
| BLE settings A/B | 6 |
| Recovery intent | 3 |
| OTA journal A/B | 24 |
| Reset history, active pointers, and namespaces | 32 |
| **Application total** | **251** |

Shot and activation history use their dedicated flash partitions rather than
NVS entries. The resulting conservative compaction margin is 2,143 entries
(89.4%). Host
tests bind the large record sizes and this arithmetic to the 84 KiB layout.
Diagnostic status and debug exports publish the installed partition size,
layout match, NVS used/free/available/total entries, namespace count, failure
count, last failing subsystem/operation/error, and flash-I/O lock timeouts.

## Combined heap/timing soak

`scripts/p2_soak.py` captures one status JSON object per interval as JSONL and
emits a machine-readable `.summary.json`. Headers, including an optional WebUI
claim, are read only from `OPENBREWBYWEIGHT_SOAK_HEADERS`; they are never copied into
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
stack below 1536 bytes, PSRAM free below 128 KiB or largest block below 68 KiB,
or sustained internal free/largest-block loss over 16 KiB, free-block growth
over 8, or fragmentation growth over 50 permille. Zero values are retained
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
alias also takes bytes for compatibility. Capture schema 2 and summary schema 3
use explicit byte names and add block topology plus per-lifecycle recovery
metrics. Review allocation-failure counter deltas for every exercised owner.

The diagnostic snapshot also carries webhook worker/client lifecycle and
bounded before/after heap samples for HTTP, Wi-Fi, OTA, webhook TLS, and Micra
TLS. Repeated `--require-lifecycle FAMILY` options require the selected family
to advance its cycle count and recover its largest block within the configured
limit; the summary reports final/worst deltas, block-count trends, and the
largest/free ratio trend per family. The analyzer also rejects
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
