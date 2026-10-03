# Resource budgets

This file versions the firmware's quantitative resource limits. The P2 filename
is retained for existing tool and evidence references. It is
an implementation contract, not a substitute for target/HIL evidence.

## Firmware image and static regions

`config/resource-baselines.json` records the current n16r8 `-O2` baseline and
the historical n8r4 `-Os` baseline. Every test or review that measures these
budgets must compile with the
`--development` profile; it enables the admin unlock and the USB Serial/JTAG
console and gives a reproducible comparison profile. Comparisons must use the
same hardware and machine profiles and the same profile on both sides. Every
supported build emits `size.json` from the linker map and
records image bytes, total linked bytes, DIRAM, flash code, and flash rodata.
The verifier requires valid measurements for all five metrics at every
optimization level.
The versioned n16r8 baseline comparisons apply to `--development -O2` builds;
the n8r4 comparisons retain their historical `--development -Os` scope. Normal
release builds and other optimization levels retain the OTA-slot, external-BSS,
and internal-placement checks. The development and release Web UIs differ, so
the development image is not always the larger one.
Small reviewed growth allowances catch regressions without coupling unrelated
toolchain padding to an exact byte count; raising a baseline or allowance
requires explicit architecture and resource review.
The current baselines were measured with ESP-IDF 6.1, its GCC 15.2 toolchain,
and the qualified `CONFIG_FREERTOS_IN_IRAM=y` build profile.

The n16r8 flash baseline represents the largest reviewed supported profile:
the Linea Micra cloud build with WebSocket observation, on
`esp32-s3-relay-x1-speaker` at `8d09ecc7`. The clean development profile with
USB Serial/JTAG measures 2,273,760 image bytes, 2,273,647 total linked bytes,
1,515,100 flash-code bytes and 587,352 rodata bytes. The versioned growth
allowances remain 38,640, 38,628, 32,768 and 16,384 bytes respectively.
TLS certificate verification and the IANA 2026d catalog remain included.
The DIRAM baseline stays at 182,518 bytes with an 8,192-byte allowance;
the current build uses 185,382 bytes. External BSS remains under its separately
reviewed 800 KiB ceiling. The 3 MiB OTA slot remains the hard image limit,
with 871,968 bytes (about 28%) free in the measured build. Static measurements
do not qualify runtime heap or stack behavior.

The n16r8 PSRAM XIP profile moves flash instructions and read-only data to
PSRAM at startup and prefers PSRAM for the NVS page cache and key hash list,
with internal fallback. N8R4 retains its prior mapping and NVS allocation.
The n16r8 Micra development+JTAG build (`esp32-s3-relay-x1-speaker-reed` /
`la-marzocco-linea-micra`, ESP-IDF 6.1, validation at `79c4788-dirty`)
measured 2,123,936 image bytes, 2,123,811 linked bytes, 1,444,516 flash-code
bytes, 517,148 `.rodata` bytes, 177,438 DIRAM bytes (32,016 internal BSS), and
106,992 external-BSS bytes. The code and rodata require roughly 1.9 MiB of
PSRAM before mapping overhead. Linker figures do not measure runtime heap;
matched target memory, settings latency and loop-gap measurements remain
required before qualification. PSRAM access may slow NVS integer operations.

Both linker maps must also keep external BSS at or below 800 KiB (819,200 bytes) and retain
`localBuzzer` and `taskProfiler` in internal DRAM. Moving their enclosing
objects to PSRAM would move synchronization state accessed under spinlocks.
The timestamped-curve Micra development profile measured 756,272 external-BSS bytes,
183,510 DIRAM bytes and a 2,188,640-byte image with the task-only pending finalizer
in PSRAM. This ceiling detects
static-placement regressions; it is not the physical PSRAM limit or a
runtime-heap measurement. The earlier 96→104 KiB increase covered the V3
half-second shot-curve store, and the 112→240 KiB increase covers the
96→512-event diagnostic log ring, whose static PSRAM ring and serial dump
snapshot grew by about 123 KiB. The current 240→800 KiB increase covers the
498,420-byte accepted-observation cache, its bounded disk workspaces, the
1201-observation sampler and task-owned finalization/serialization staging.
The immutable persistence image is separately allocated in external heap.

## Runtime placement and allocation

| Resource | Placement and bound |
|---|---|
| Network work buffer | external, measured 628,104 bytes on ESP32-S3, bounded at 640 KiB; includes the 498,400-byte curve read copy, 22,016-byte curve JSON, 23,552-byte row JSON, 40,960-byte status JSON and dedicated curve staging; handlers share the work-buffer mutex |
| HTTP response send | complete assets and JSON use HTTPD Content-Length responses; streamed bodies retain chunked transfer. Source buffers pass directly to HTTPD's default socket send, which copies into lwIP; no application bounce buffer or extra copy |
| NVS metadata cache | PSRAM preferred with internal fallback on n16r8; n8r4 retains its existing placement; flash I/O still uses the internal scratch below |
| Shot-curve store | external, 498,420-byte cache for 100 records of 4,984 bytes; bounded block-header index and two 5,088-byte disk/verification workspaces belong to the same owner; immutable worker image is separately external |
| Curve capture/finalization | external task-owned 1201-observation sampler and pending/finalization snapshots; no large curve local on control or HTTP stacks; the established mutex-protected published status remains internal, bounded at 6,656 bytes (6,552 on host) |
| Shared flash-I/O scratch | internal heap, 3,328-byte capacity for one 3,304-byte PersistedSettings record; slots are read, written, and verified sequentially under the flash-I/O lock, with no PSRAM fallback; the larger partition stores transfer in 1 KiB chunks staged through the same scratch |
| USB serial output | internal heap, 2,064 bytes for the eight-record ESP log queue; one external 2,560-byte CLI reply buffer; startup failures free both allocations, and successful startup retains one boot-lifetime owner |
| Micra cloud workspace | external and lazy; a 6,344-byte work buffer on ESP32-S3 holds identity, tokens, authorization header, and client state while cloud observation is active, plus one request-scoped 16 KiB buffer whose mutually exclusive request-body and response phases share storage (22,728 bytes combined, excluding HTTP/TLS library allocations); Disconnect, disabled observation, STA loss, and AP entry destroy the client and free both blocks |
| Micra WS/STOMP scratch | PSRAM-only reusable block, capped at 24 KiB; includes 17410 B accumulator, two 1025 B header scratch arrays, signed-header/CONNECT storage, and 60 × 12 B rate bins; retained across shot/acquisition pauses, freed after callback quiescence for API/disable/identity/network/maintenance changes |
| Micra WS SDK allocations | separate 8192 B internal task stack, core 0 priority 1; fixed RX/TX 1024 B ordinary-heap buffers, event/transport objects and WSS TLS are separate; dynamic SDK buffers and auto-reconnect disabled |
| Micra/Webhook TLS allocations | external through the Micra profile's mbedTLS allocator (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` from `sdkconfig.defaults.micra`); dynamic record, certificate, handshake, and session objects never fragment internal DRAM on Micra-profile builds and are freed through the matching capability allocator. Other machine profiles keep mbedTLS internal, so webhookS there still draws handshake memory from internal DRAM |
| Profiler processing workspace | external, at most 4 KiB, only while running |
| Scale profiler workspace | external, only while a manual capture exists: 256 KiB record buffer, header/context and a bounded per-signal observation cache (32 bytes per signal), and a 32 KiB ordering index after stop (combined ceiling 320 KiB, enforced including the index); freed after the trace is saved and no download is streaming it; capture storage is a dedicated 260 KiB flash partition with a header-last commit |
| Profiler kernel capture | internal, at most 4 KiB, only while running |
| Settings handoff | one external mailbox for the 3,312-byte settings blob and revision, and one internal byte queued; no full settings copy in the queue or receiver |
| Web command | trivially copyable, at most 416 bytes; configuration and network payloads share a discriminated union |
| Radio settings snapshot | at most 224 bytes; full 3,312-byte settings remain for durable mutations |
| Wi-Fi static pools | eight RX and sixteen TX internal DMA buffers reserved while Wi-Fi is initialized, approximately 1.6 KB each; about 19.2 KB more internal RAM than the former six RX/six TX pools |
| Wi-Fi packet queues | dynamic RX and cache TX each bounded at 32 packets; RX block-ack window 16. Cache TX holds overflow packets when static TX buffers are busy; it is not an equivalent preallocated DMA pool |
| TCP capacity | 16 KiB send/receive limits and a 16-entry receive mailbox per connection; payload memory grows under load, with PSRAM preferred where supported. Fully loaded bidirectional payload capacity is 27,008 bytes higher per connection than the former 2,880-byte limits, before metadata |
| mDNS responder | NetworkService-owned; mDNS 1.13.1 places its 4096-byte priority-1 task stack on core 0 in internal RAM on n16r8 for core dumps, and in PSRAM on n8r4; dynamic responder allocations remain in PSRAM; one persistent UDP socket; freed once in `OpenBrewByWeightNetwork::stop()` |
| Fixed buzzer melodies | at most 8 notes each; custom tune capacity remains 250 notes |
| JSON parser | PSRAM only; Web input remains at most 2047 bytes / 128 values; the Micra worker explicitly admits at most 16 KiB / 1024 values for bounded cloud responses; nesting remains 32 |
| BBW adaptive candidates | control-owned fixed RAM, at most 3,000 bytes for eight presets; 20 observations and five trajectory anchors each |

The timestamped-curve Micra development image has compiled entry frames of
3,216 bytes for control-status publication, 480 bytes for finalization scheduling,
6,944 bytes for the status handler and 4,720 bytes for integration. The control
loop has an 8,192-byte stack and HTTPD has 11,264 bytes. These frames exclude
nested calls and interrupt overhead; target stack-watermark qualification is
still required. Curve helpers fill their owned destinations directly, and
control-status publication initializes its existing object in place, avoiding
large return-value and default-aggregate temporaries.

Network command builders must activate their union member with
`setNetworkType()` before writing credentials. Preset metadata remains outside
the union because a preset operation also carries configuration. Settings
schema 3 uses a 3,312-byte blob for the bounded Micra cloud account, selected
machine, and per-scale friendly names. Compatible schema 1/2 blobs are CRC-checked before read-only migration to
schema 3. The connection byte uses prior padding; the checksum stays at offset
3304. Trial-boot writes wait for OTA confirmation to retain rollback settings.

History V5 retains an exact bounded preset-name snapshot and transfers through
the shared chunked flash-I/O path. The separate last-shot V4 record retains the
same provenance. The English authoring caps are 82,000 HTML bytes, 237,000 JavaScript bytes and
319,000 combined source bytes. Compressed runtime remains capped at 44,000,
secondary modules at 9,800 and combined assets at 116,000 bytes. The selectable
WS implementation and its correctness fixes are included in the reviewed flash
baseline above. The OTA-slot, DIRAM, external-BSS and other asset limits remain
unchanged. The reference clean development build with WS measures 2,273,760 image
bytes, 2,273,647 linked bytes, 1,515,100 flash-code bytes and 587,352 rodata bytes.
The HTTP-only dependency-preparation image was 2,228,768 bytes; WS and its
correctness fixes add about 44 KiB, including differences in version metadata.
Linked DIRAM rises by 448 bytes to 185,382; external BSS stays at 756,272.
The clean image retains 871,968 bytes in its OTA slot.
These are linked measurements; handshake/streaming peaks still require target
qualification. Fixed-version English assets measure 115,211 combined gzip
bytes and 9,685 secondary-module gzip bytes. The rounded compressed caps retain
789 and 115 bytes respectively; other per-asset and source caps stay unchanged.

Every new setting must include concise, natural help that explains its effect on
the barista's workflow, including what changes when an option is enabled or
disabled. Adding a setting is expected to increase the Web UI budget, and the
applicable source, compressed-asset, and firmware limits must be raised through
the normal measured review when necessary. Removing, shortening, or making help
less useful merely to fit an earlier budget is not acceptable: a clear,
friendly, well-constructed UI takes priority over preserving the previous Web UI
byte allowance. Per-asset caps remain independently enforced by Web contract
tests, so one asset cannot consume all combined headroom.

Both supported partition tables reserve `shotcurve` at custom subtype
`0x40`, exactly `0xCC000` (816 KiB): 101 × 8192-byte record blocks
plus two separate 4096-byte epoch sectors. The maximum used record is 5012
bytes, leaving 3180 reserved bytes per block. A shorter curve erases/programs
only its used length and ignores stale bytes beyond it. On n8r4 the partition
occupies `0x69E000`–`0x769FFF`; on n16r8 it occupies
`0x63E000`–`0x709FFF`. The unmounted filesystem reservation shrinks;
NVS, both OTA slots, shot summaries, activation history and crash storage
retain their addresses. The installer permits the exact previous project
layout to transition over USB without erasing unrelated saved data.

Capability samples use `INTERNAL|8BIT` and `SPIRAM|8BIT`, including the PSRAM
minimum-free watermark. The retained boot capture uses at most 768 bytes of
static PSRAM plus one internal static task mutex and an atomic sampling flag.
It also samples `INTERNAL|DMA`; the overlapping capability pools are not additive.
First-stage samples and approximately 100 ms health-task samples maintain separate
hold/post-release minima, without resetting lifetime watermarks. Sampling stops
60 seconds after release, while records remain available through
[`BOOT_HEAP`](SERIAL_CLI.md#capture-startup-memory) until reset. These sampled
minima can miss shorter dips; missing phases/stages are explicit in the report.
Diagnostic `memoryAllocations` reports cumulative
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
Micra TLS, and webhook owners retain only a bounded last before/after sample,
signed deltas, cycle count, stale-start count, worst free/largest loss, and
maximum free-block increase. Sampling occurs outside owner locks and outside
OTA chunk/cache-off work; only the fixed result is copied under the existing
owner mutex.

The current n16r8 Micra development+JTAG build measures 182,518 linked DIRAM bytes.
The one-record scratch reserves 3,328 bytes in its lazy runtime allocation,
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
stack below 1536 bytes, PSRAM free below 128 KiB or largest block below 76 KiB,
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
bounded before/after heap samples for HTTP, Wi-Fi, OTA, webhook, and Micra
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

## WebSocket memory qualification

Diagnostic `lineaMicra.websocket` reports retained application bytes/placement,
allocation failures, internal free/largest-block deltas for connect and stop,
and internal/PSRAM minima plus PSRAM free/largest values sampled at lifecycle
boundaries. `heapLifecycle.micraTls` remains HTTP-only; `micraWebsocket` records
WS connection history. Counts exclude TLS, SDK stack and allocator overhead.
These are bounded samples, not continuously observed peaks or proof of placement.
The internal stack and SDK buffers remain independent reservations even when TLS
uses PSRAM. Target comparison must include simultaneous HTTP/WSS handshakes,
1000 pause/reconnect cycles, full disable and OTA maintenance. Firmware/linker
sizes and XIP/static PSRAM reservations do not measure runtime heap pressure.
