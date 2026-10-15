# Resource budgets

This file versions the firmware's quantitative resource limits. The P2 filename
is retained for existing tool and evidence references. It is
an implementation contract, not a substitute for target/HIL evidence.

## Firmware image and static regions

`config/resource-baselines.json` records the current n16r8 `-O2` baseline and
the historical n8r4 `-Os` baseline. Every test or review that measures these
budgets must compile with the
`--development` profile; it enables the admin unlock and the USB Serial/JTAG
console and gives a reproducible comparison profile. This is enforced, not
just prose: the R2/R3 validation gate compiles every supported profile pair
with `--development` (FIRMWARE_BUDGET_OPTIONS in scripts/dev), a tooling test
pins that composition for both risk levels, and any other build profile makes
the resource verifier print "budget evidence requires a --development build"
instead of comparing baselines. Comparisons must use the
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
allowances remain 32,768 and 16,384 bytes for flash code and rodata; the
image and total linked allowances were raised from 38,640 and 38,628 bytes to
38,768 and 38,756 bytes for the reviewed frosted-glass sliding navigation
pill assets (about 250 compressed bytes, partly absorbed by flash
compression), again to 43,520 and 43,508 bytes for the Stats duration and
gram-weight metric icons and the view-scoped Diagnostic WebSocket stream
(measured 2,317,248 image bytes: the icons consumed the remaining headroom
and the Diagnostic stream adds about 4.2 KB — a bounded per-field diagnostic
projection with per-session fingerprints, the socket subscription client, and
the shared live section renderer, with flash code and rodata growth staying
inside their separate allowances and DIRAM growing 528 bytes for two
fingerprint slots), and once more to 43,904 and 43,896 bytes when the
Diagnostic stream gained its Scale weight field (measured 2,317,600 image
bytes through the validation gate's machine-type-specific Web UI pipeline;
the two follow-up measurements differ by 48 bytes of asset/toolchain
padding for the same source), and to 44,016 and 44,008 bytes when the Home
shot card gained its no-scale timer layout (measured 2,317,712 image bytes;
the card's new `scaleAvailable` field plus the slightly larger localized
runtime account for about 110 bytes after flash compression). The browser-local
no-scale timer raises those allowances by another 320 bytes, to 44,336 and
44,328 bytes. The reference development build measured 2,318,032 image bytes
and 2,317,919 linked bytes. This change adds browser clock state and animation
only: DIRAM remains 189,078 bytes, flash code remains 1,546,752 bytes, and
rodata measures 599,364 bytes within its existing allowance. The combined
compressed Web asset cap remains 120,700 bytes; 100 bytes of unused shell
allowance move to the runtime cap. Firmware task ownership, control timing,
OTA identity and partitions remain unchanged.
Urgent control-gate publication and the no-scale timer's stop fade add another
640 bytes of reviewed image/linked allowance, to 44,976 and 44,968 bytes.
The mobile full-screen Home shot card and the Brew by Weight master toggle
raise them once more by 2,048 bytes, to 47,024 and 47,016 bytes (measured
2,320,240 image bytes and 2,320,127 linked bytes). Both are browser-local
Web UI changes with no firmware task, control-timing, or memory-placement
impact; flash code, rodata, DIRAM, and external BSS remain within their
existing allowances. The combined compressed Web assets measure 122,530
bytes; the per-asset caps enforced by the Web contract tests are unchanged.
The record pages' move onto the owned WebSocket raises the image and linked
allowances by another 2,048 bytes, to 49,072 and 49,064 bytes (measured
2,321,920 image bytes and 2,321,799 linked bytes): the History and Stats
stream senders, the shared record UI/row projections, and the stream client
replace the REST read handlers. Flash code grows 32,700 bytes (inside its
32,768-byte allowance), rodata 14,844 and DIRAM 6,704 bytes (inside theirs);
external BSS is unchanged. The combined compressed Web assets measure
123,231 bytes; their per-asset caps are enforced by the Web contract tests.
The post-landed record-stream review raises the flash-code allowance by 512
bytes, to 33,280 (measured 1,548,180): the senders' no-change bookkeeping
now stores the epoch on the fingerprint-suppression path and on snapshot
sends, and the history fingerprint became component-wise over the raw
record bytes. Image, rodata, DIRAM, and external-BSS allowances are
unchanged.
The SoftAP onboarding feature (captive-portal DNS, probe redirect, SoftAP
admin bypass, live-apply save, /setup view) raises the flash-code allowance
by another 512 bytes, to 33,792, and the rodata allowance by 2,048 bytes,
to 18,432 (measured 1,548,556 flash-code and 605,644 rodata bytes on the
development Micra build). Code growth is the vendored AsyncUDP/DNSServer
reply path plus the bypass, probe-redirect, and live-apply handlers; rodata
is dominated by the embedded onboarding assets (the /setup partial, its
view module, and route wiring measured 129,368 combined compressed bytes in
the Web contract gate), which consume the roughly 1,940 rodata bytes the
record-stream change had left. The measured 2,321,200-byte image and
2,321,075 linked bytes stay inside their existing 49,072/49,064-byte
allowances, DIRAM grows 480 bytes for the DNSServer object and the stream
session flag (well inside its 8,192-byte allowance), and the reed/Silvia
development profile keeps passing with negative deltas against this
baseline. Firmware task ownership, control timing, OTA identity and
partitions remain unchanged. The /setup connecting-countdown assets push the measured
development rodata to 605,900 bytes (18,548 over baseline), so the rodata
allowance rises once more, to 19,456 bytes: the roughly 900 remaining bytes
deliberately absorb the documented version/asset-tag padding wobble so
routine commits do not trip the gate. Flash code measures 1,548,664 bytes
(228 bytes inside its allowance), the image 2,321,552 and linked 2,321,439
bytes, and DIRAM is unchanged.
The additional atomic priority flag retains the single HTTP workspace owner;
control only notifies Network and never waits for serialization. The stop fade
is browser animation state, with no firmware task or control-timing changes.
The runtime cap receives another 400 bytes from unused shell allowance, and
the combined compressed asset cap increases by 200 bytes to 120,900 bytes.
Flash-code, rodata, DIRAM and external-BSS limits remain unchanged, as do the
OTA identity and 3 MiB slot. The final Micra development image measures
2,318,576 bytes, with 2,318,459 linked bytes, 1,546,876 flash-code bytes and
599,780 rodata bytes. DIRAM remains 189,078 bytes and external BSS remains
757,144 bytes. The three-profile gate verifies these limits; combined assets
measure 120,888 compressed bytes within the 120,900-byte cap.
TLS certificate verification and the IANA 2026d catalog remain included.
The DIRAM baseline stays at 182,518 bytes with an 8,192-byte allowance;
the current build uses 189,078 bytes. External BSS remains under its separately
reviewed 800 KiB ceiling. The 3 MiB OTA slot remains the hard image limit,
with 827,152 bytes (about 26%) free in the measured build. Static measurements
do not qualify runtime heap or stack behavior.
The loop diagnostics and control-loop slimming series raises the image,
linked, flash-code, and rodata allowances once more, to 52,272, 52,264,
34,196, and 21,300 bytes. Two work streams share the growth: the loop
diagnostics commits (input sub-phase attribution, true own-CPU pairing with
the lock-free counter path) that landed above the last reviewed allowance,
and the status-publish slimming plus all-loop optimization series (status
sub-phase profiling with shot-store and control-status lock-wait telemetry,
the generation-guarded store gather, the per-dispatch stream fetch hoist,
and the Stats records cache with per-row curve fetches). The reference
development Micra build measures 2,325,968 image bytes, 2,325,847 linked
bytes, 1,549,232 flash-code bytes, 608,588 rodata bytes, DIRAM at 185,094
bytes (2,576 over baseline, well inside its allowance), and external BSS
at 762,248 of 819,200 bytes; the series also returns about 492 KB of
runtime PSRAM heap by dropping the whole-store curve copy from the network
workspace, which these ELF metrics do not capture. Each allowance carries
64 bytes of headroom over the measurement, following the established
padding-wobble convention. Firmware task ownership, control timing, OTA
identity and partitions remain unchanged.
The persistent audit log series (curated RAM latch, flash partition store,
Diagnostics surfacing with hunt sessions and CLI verbs, alongside the
quick-weight guard-range dial and the sort-control labels) raises the
image/linked allowances to 59,400/59,400 bytes and the rodata allowance to
28,500 bytes. The reference development Micra build measures 2,332,912
image bytes, 2,332,787 linked bytes, and 615,644 rodata bytes. Of the
28,292 rodata bytes over baseline, 2,808 were the documented pre-existing
debt from the task-mutex holder telemetry change; the remainder is the
audit export/stream/CLI string surface and the enlarged embedded Web
assets, whose compressed caps are separately enforced by the Web contract
gate. Flash code stays inside its 36,200-byte allowance, DIRAM inside
8,192, and external BSS well under its ceiling. The new auditlog partition
carves reserved filesystem space without moving any existing partition, so
OTA identity and slot layout are unchanged.
The Web UI stream takeover repair (valid audit-frame JSON on the diagnostic
stream plus the 4001 takeover close shared by the dispatch and rejected-bind
paths, with the off-Home reachability recovery note in the embedded runtime)
raises the image/linked allowances to 59,648/59,636 bytes. The reference
development Micra build measures 2,333,344 image bytes and 2,333,219 linked
bytes — 59,584/59,572 over baseline, with the allowances keeping the
established 64 bytes of padding-wobble headroom. Flash code, rodata, DIRAM,
external BSS, task ownership, control timing, OTA identity, and partitions
all remain inside their existing limits.
The decigram goal migration (settings v3→v4 staged widening in the NVS read
path, decigram records across shot log v3 and the last-shot blob, one-decimal
JSON surfaces, and the tenth-precision quick-weight dial with its 0.1-step
Settings validation) raises the image/linked allowances to 61,584/61,584
bytes and the rodata allowance to 28,660 bytes. The reference development
Micra build measures 2,335,280 image bytes, 2,335,167 linked bytes, and
615,948 rodata bytes — 61,520/61,520/28,596 over baseline, with the
allowances keeping the established 64 bytes of padding-wobble headroom.
Flash code (36,012 over baseline), DIRAM (5,056), and external BSS (771,720
of 819,200) stay inside their existing allowances. Control timing, OTA
identity, and partitions are unchanged; the widened settings blob keeps the
dual-slot NVS write path inside its 3,360-byte budget.
The Diagnostics audit detail work (the full-log text export endpoint with
its chunked sink, and the composed audit detail rebuilt from the stored
record arguments at the JSON surfaces) raises the image/linked allowances to
65,264/65,264 bytes, the flash-code allowance to 38,896 bytes, and the
rodata allowance to 29,508 bytes. The reference development Micra build
measures 2,338,960 image bytes, 2,338,835 linked bytes, 1,553,932 flash-code
bytes, and 616,796 rodata bytes — 65,200/65,188/38,832/29,444 over baseline,
keeping the 64-byte padding-wobble headroom. Manual UTC formatting (instead
of strftime) keeps the newlib locale machinery out of the image. DIRAM
(5,056 over baseline) and external BSS stay inside their existing
allowances; control timing, OTA identity, and partitions are unchanged.
The Diagnostics audit pagination and boot-shaping suppression work (the paged
audit table with its change-gated repaint, plus the shaper-held gate on the
health heap alert) raises the image/linked allowances to 65,696/65,684
bytes, the flash-code allowance to 38,976 bytes, and the rodata allowance to
29,860 bytes. The reference development Micra build measures 2,339,392
image bytes, 2,339,267 linked bytes, 1,554,012 flash-code bytes, and
617,148 rodata bytes — 65,632/65,620/38,912/29,796 over baseline, keeping
the 64-byte padding-wobble headroom. The growth is the enlarged embedded
Web assets (whose compressed caps the Web contract gate enforces separately)
plus a few bytes of alert-gate code. DIRAM (5,056 over baseline) and
external BSS stay inside their existing allowances; control timing, OTA
identity, and partitions are unchanged.
The smooth-cut scale-loss backup (loss-deadline regimes, per-preset arrival
statistics read from the shot log, the idle deadline projection and the
statistics-epoch commands) raises the image/linked allowances to
67,536/67,528 bytes and the flash-code allowance to 41,796 bytes. The
reference development Micra build measures 2,341,232 image bytes, 2,341,111
linked bytes and 1,556,832 flash-code bytes — 67,472/67,464/41,732 over
baseline, keeping the 64-byte padding-wobble headroom. Rodata (616,412,
29,060 over baseline), DIRAM (4,704 over baseline) and external BSS
(771,816 of 819,200) stay inside their existing allowances; control timing,
OTA identity, and partitions are unchanged.
The smooth-cut Web UI and its firmware wire (preset editor with the review
graph, Home pills and cards, Shared settings regrouping, card and stats
fields; owner-authorized 2026-10-14) raises the image/linked allowances to
75,536/75,528 bytes, the flash-code allowance to 42,868 bytes and the rodata
allowance to 36,052 bytes. The reference development Micra build measures
2,349,232 image bytes, 2,349,111 linked bytes, 1,557,904 flash-code bytes and
623,340 rodata bytes — 75,472/75,464/42,804/35,988 over baseline, keeping the
64-byte padding-wobble headroom. The growth is mostly the embedded Web assets
(143,513 bytes combined gzip, whose caps the Web contract gate enforces
separately). DIRAM (4,704 over baseline) and external BSS (771,816 of
819,200) stay inside their existing allowances; control timing, OTA identity,
and partitions are unchanged.
Per-preset shot statistics and grind hints (presets JSON statistics, stacked
per-verdict Stats chart) raise the image/linked allowances to 77,536/77,532
bytes, the flash-code allowance to 44,344 bytes and the rodata allowance to
36,564 bytes. The reference development Micra build measures 2,351,232 image,
2,351,115 linked, 1,559,380 flash-code and 623,852 rodata bytes —
77,472/77,468/44,280/36,500 over baseline, with the 64-byte headroom kept.
The Web UI follow-ups (review-graph window lines, toolbar spacing, hero
copy) bring rodata to 623,980 bytes (36,628 over baseline); its allowance
moves to 36,692 bytes.
The Home **How shots stop** card that replaces Quick Settings brings the
reference development Micra build to 2,351,296 image, 2,351,171 linked,
1,558,428 flash-code and 625,164 rodata bytes — 77,536/77,524/43,328/37,812
over baseline. The image/linked allowances move to 77,600/77,588 bytes and the
rodata allowance to 37,876 bytes, keeping the 64-byte headroom; flash-code
stays inside its allowance.
The Home target sheet's ruler with hold-to-repeat −/+ buttons and dose/ratio
pills brings the build to 2,351,520 image, 2,351,395 linked and 625,388 rodata
bytes — 77,760/77,748/38,036 over baseline. The image/linked allowances move to
77,824/77,812 bytes and the rodata allowance to 38,100 bytes, keeping the
64-byte headroom.
Release inertia on the target ruler, the disabled-until-changed Save button
and the frosted-glass removal bring the build to 2,351,616 image, 2,351,491
linked and 625,484 rodata bytes — 77,856/77,844/38,132 over baseline. The
image/linked allowances move to 77,920/77,908 bytes and the rodata allowance to
38,196 bytes, keeping the 64-byte headroom.
The preset graph cut pill, Home graph copies and memory-audit hardening bring
the build to 2,352,224 image, 2,352,111 linked and 627,100 rodata bytes, and
the preset graph's first-drop pace start to 2,353,136 image, 2,353,015 linked
and 627,260 rodata bytes — 79,376/79,368/39,908 over baseline. The
image/linked allowances move to 79,440/79,432 bytes and the rodata allowance to
39,972 bytes, keeping the 64-byte headroom; flash-code stays inside its
allowance.
Shorter Web UI strings (en.json value-only change) and the other session's changes bring the
build to 2,351,760 image, 2,351,643 linked and 625,644 rodata bytes on this branch, 78,000/77,996/38,292
over baseline (main at d535c6ad measures 2,353,200 image, 79,440 over). The image/linked/rodata allowances
move to 78,064/78,060/38,356 bytes, keeping the 64-byte headroom.
The cup, touch and auto-tare robustness series (cup removal as a drop step,
provisional empty anchors, touch handling of heavy steps, software zero after
a failed start tare, first-drop hardening, and the empty-offset diagnostic)
adds about 2.5 KB of control code and brings the build to 2,354,400 image,
2,354,279 linked, 1,560,672 flash-code and 626,012 rodata bytes —
80,640/80,632/45,572/38,660 over baseline (the parent commit de305462 measures
2,351,904 image and 1,558,260 flash code). The image/linked/flash-code
allowances move to 80,704/80,696/45,636 bytes, keeping the 64-byte headroom;
rodata stays inside its allowance.
The follow-up (first drop after a gush with a small minimum cup, and the
empty-pan wait age with its profiler event and Home text) brings the build
to 2,355,408 image, 2,355,287 linked, 1,561,552 flash-code and 626,108
rodata bytes — 81,648/81,640/46,452/38,756 over baseline. The
image/linked/flash-code allowances move to 81,712/81,704/46,516 bytes,
keeping the 64-byte headroom; rodata stays inside its allowance.
The Stats shot cursor pill brings the build to 2,355,520 image, 2,355,395
linked, 1,561,516 flash-code and 626,252 rodata bytes —
81,760/81,748/46,416/38,900 over baseline. The image/linked allowances move
to 81,824/81,812 bytes, keeping the 64-byte headroom; flash code and rodata
stay inside their allowances.
The webhook reliability change (asynchronous receiver lookup, one connection
per delivery, lock-free worker state reads) brings the build to 2,356,480
image, 2,356,367 linked, 1,562,424 flash-code and 626,316 rodata bytes —
82,720/82,720/47,324/38,964 over baseline, measured from a Git worktree whose
longer source paths add 144 rodata bytes. The image/linked/flash-code
allowances move to 82,784/82,784/47,388 bytes, keeping the 64-byte headroom;
rodata stays inside its allowance.
OTA session expiry on the polled routes and the network tick, the gated
abort and deferred journal erasure bring the build to 2,356,672 image,
2,356,551 linked, 1,562,624 flash-code and 626,300 rodata bytes (same
worktree) — 82,912/82,904/47,524/38,948 over baseline. The
image/linked/flash-code allowances move to 82,976/82,968/47,588 bytes,
keeping the 64-byte headroom; rodata stays inside its allowance.
Wi-Fi reconnect backoff, the same-origin UI stream check and the per-dispatch
UI stream budget bring the build to 2,357,072 image, 2,356,955 linked,
1,562,916 flash-code and 626,412 rodata bytes (same worktree) —
83,312/83,308/47,816/39,060 over baseline. The image/linked/flash-code
allowances move to 83,376/83,372/47,880 bytes, keeping the 64-byte headroom;
rodata stays inside its allowance.
Moving the Home preset render caches into the PSRAM network work buffer
lowers DIRAM to 187,366 bytes (−1,280), while trailer-digest OTA reads and
edge-gated Wi-Fi power-save reconciliation bring flash code to 1,563,272
bytes (48,172 over baseline, same worktree). The flash-code allowance moves
to 48,236 bytes, keeping the 64-byte headroom; image, linked size and rodata
stay inside their allowances.
Tolerant Micra dashboard decoding and the connect-relative STOMP handshake
timeout bring flash code to 1,563,344 bytes (48,244 over baseline); the
flash-code allowance moves to 48,308 bytes, keeping the 64-byte headroom.
The OTA committed-state result and the transfer-id check inside writeRange
bring flash code to 1,563,488 bytes (48,388 over baseline); the flash-code
allowance moves to 48,452 bytes, keeping the 64-byte headroom.
Moving the task-profiler published report (2,512 bytes) and the OTA state
singleton (about 1.6 KiB) into PSRAM BSS, with their mutexes and atomic flags
kept in internal statics, lowers DIRAM to 183,206 bytes (−4,160 from 187,366)
and raises external BSS to 776,008 bytes; image and flash code shrink slightly
and stay inside their allowances. The DIRAM baseline moves down to 183,206
bytes with its unchanged 8,192-byte allowance so the saving cannot regress
unnoticed.
Keeping the unused RMT driver's ISR and encoder code in flash
(`CONFIG_RMT_*_IN_IRAM` cleared) lowers DIRAM to 178,410 bytes (−4,796) and
grows flash code to 1,567,988 bytes (52,888 over baseline), so the flash-code
allowance moves to 52,952 bytes, keeping the 64-byte headroom; the DIRAM
baseline moves to 178,410 bytes.
Keeping the GDMA control and ISR code in flash (with the SPI master/slave ISR
options that would force it back into IRAM) lowers DIRAM to 177,434 bytes
(−976) and brings flash code to 1,569,100 bytes (54,000 over baseline), the
image to 2,357,200 bytes (83,440 over) and the linked size to 2,357,075 bytes
(83,428 over); those allowances move to 54,064/83,504/83,492 bytes, keeping
the 64-byte headroom, and the DIRAM baseline moves to 177,434 bytes. Across the
three steps linked DIRAM falls 9,932 bytes; IRAM text is now 123,595 bytes.

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
`localBuzzer` and `taskProfiler` in internal DRAM: both hold synchronization
state (mutexes, atomics) that must not live in PSRAM. The profiler's published
report (`taskProfilerReport`) must instead reside in external BSS.
The same map check now requires the published control scalars, control gate and
their mutexes to stay internal, and the published curve payload to reside in
external BSS. Runtime session fingerprints remain part of the explicitly
external Network allocation.
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

## Home shot stream target measurements

The 2026-10-05 comparison used the same ESP32-S3 N16R8, speaker/Micra
development pair, ESP-IDF 6.1, retained settings/history, disconnected scale,
disabled scale recording and existing cloud/webhook connections. Baseline
`0501da90` and corrected stream firmware `93dc21a4` were USB-installed and
sampled with the same procedure through 64 seconds after restart. These are
measured reference workloads, not new limits or full hardware qualification.

| Measurement, bytes unless stated | Baseline | Home stream | Difference |
| --- | ---: | ---: | ---: |
| Settled internal free heap | 80,971 | 80,063 | -908 |
| Lifetime minimum internal heap | 31,232 | 30,240 | -992 |
| Settled largest internal block | 40,960 | 40,960 | 0 |
| Reported PSRAM heap capacity | 5,467,008 | 5,462,016 | -4,992 |
| Settled PSRAM free heap | 4,187,424 | 4,182,512 | -4,912 |
| Lifetime minimum PSRAM heap | 4,132,076 | 4,135,112 | +3,036 |
| Settled largest PSRAM block | 4,128,768 | 4,128,768 | 0 |
| Linked DIRAM | 187,158 | 187,494 | +336 |
| External BSS | 756,976 | 757,144 | +168 |
| Firmware image | 2,292,288 | 2,310,512 | +18,224 |

Reported PSRAM capacity changed with the image; allocated PSRAM at the settled
comparison point was 80 bytes lower. Free-heap differences alone therefore do
not describe dynamic allocation growth. Separate boots measured internal minima
of 31,232–37,088 bytes for the baseline and 30,184–36,196 bytes for the corrected
firmware. Startup traces placed the transient dips around existing outbound
connections and the 52 KiB startup reservation; compare matched workloads rather
than attributing a lifetime minimum to the Home stream alone.

Two 100-reconnection runs recovered their initial internal free/minimum/largest
figures. A bound/overlapping connection temporarily reduced the largest internal
block by 2 KiB; it recovered after cleanup. Idle stream CPU samples were
0.35–0.46%, versus 0.25–0.30% settled without a stream. A 200-request resync burst
produced 25 snapshots over the burst and drain interval. A stalled reader closed
and a fresh stream recovered; concurrent Home/Stats/History/OTA-status requests
completed, with a worst observed response of 744 ms under three HTTP workers.
No additional external-allocation fallback or HCI drops were observed. A full
2,310,512-byte same-image OTA transfer also completed with the stream occupied;
the stream recovered after socket recycling during upload and after reboot.
The new boot's image digest matched and automatic confirmation succeeded.

The target's 396-point canonical snapshot was 4,730 bytes and matched Stats
weights, timestamps and breaks exactly. An offline maximum-width 1201-point
curve with every possible break and all event markers serialized to 20,703
bytes inside the existing 22,016-byte curve workspace. Active scale/drip timing,
maximum-curve target sends, HTTP-task stack margin and server-stop overlap remain
manual qualification requirements. OTA power-cut, safety-abort and rollback
cases also remain pending; the successful full transfer does not qualify them.

## Runtime placement and allocation

| Resource | Placement and bound |
|---|---|
| Network work buffer | external, bounded at 640 KiB; includes the existing 498,400-byte curve read copy in a reusable coherent Stats capture, 22,016-byte curve JSON, 26,112-byte row JSON, 40,960-byte status JSON, dedicated curve staging, 2,816 bytes of session fingerprints and a bounded page header/cache key; handlers share the work-buffer mutex. The earlier 629,376-byte measurement predates this layout; verify the current size in the matched firmware build |
| HTTP response send | complete assets and JSON use HTTPD Content-Length responses; streamed bodies retain chunked transfer. Source buffers pass directly to HTTPD's default socket send, which copies into lwIP; no application bounce buffer or extra copy |
| NVS metadata cache | PSRAM preferred with internal fallback on n16r8; n8r4 retains its existing placement; flash I/O still uses the internal scratch below |
| Shot-curve store | external, 498,420-byte cache for 100 records of 4,984 bytes; bounded block-header index and two 5,088-byte disk/verification workspaces belong to the same owner; immutable worker image is separately external |
| Curve capture/finalization | external task-owned 1201-observation sampler and pending/finalization snapshots; the published scalar status stays internal, bounded at 2,048 bytes; its 4,955-byte curve array payload moves to PSRAM (plus alignment), with the same mutex/version covering both parts. The full reader snapshot stays bounded at 6,912 bytes in the external HTTP workspace; no control-path allocation or large stack local |
| Home live stream | two fixed internal session contexts and one coalesced dispatch on the existing priority-1 tasks; 128 Home and 224 Diagnostic field fingerprints per session occupy 2,816 bytes in the external workspace; boot/clock anchors stay internal. Shared 26,112-byte external row workspace and 22,016-byte curve scratch, with no extra TX allocation; 1201 curve points maximum, 100 ms progress cadence and no unchanged data; four HTTP sockets retained |
| Shared flash-I/O scratch | internal heap, 3,328-byte capacity for one 3,304-byte PersistedSettings record; slots are read, written, and verified sequentially under the flash-I/O lock, with no PSRAM fallback; the larger partition stores transfer in 1 KiB chunks staged through the same scratch |
| USB serial output | internal heap, 2,064 bytes for the eight-record ESP log queue; one external 2,560-byte CLI reply buffer; startup failures free both allocations, and successful startup retains one boot-lifetime owner |
| Micra cloud workspace | external and lazy; a 6,344-byte work buffer on ESP32-S3 holds identity, tokens, authorization header, and client state while cloud observation is active, plus one request-scoped 16 KiB buffer whose mutually exclusive request-body and response phases share storage (22,728 bytes combined, excluding HTTP/TLS library allocations); Disconnect, disabled observation, STA loss, and AP entry destroy the client and free both blocks |
| Micra WS/STOMP scratch | PSRAM-only reusable block, capped at 24 KiB; includes 17410 B accumulator, two 1025 B header scratch arrays, signed-header/CONNECT storage, and 60 × 12 B rate bins; retained across shot/acquisition pauses, freed after callback quiescence for API/disable/identity/network/maintenance changes |
| Micra SDK transport buffers | HTTP RX/TX 4096 B each; WebSocket RX/TX 4096 B each plus a separate 4096 B upgrade buffer. Ordinary malloc prefers PSRAM above ALWAYSINTERNAL=2048, with possible internal fallback. The 6144 B internal WebSocket task stack remains on core 0 priority 1; event/transport objects and WSS TLS are separate; dynamic SDK buffers and auto-reconnect disabled |
| Micra TLS allocations | external through the Micra profile's mbedTLS allocator (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` from `sdkconfig.defaults.micra`); dynamic record, certificate, handshake, and session objects use PSRAM without internal fallback. Generic webhooks accept HTTP only and do not create TLS handshakes |
| Profiler processing workspace | external, at most 4 KiB, only while running |
| Profiler published report | external BSS, 2,512 bytes; written and copied under the profiler's internal mutex by tasks only |
| OTA state singleton | external BSS, about 1.6 KiB (identity, tags, journal bookkeeping, tag scanner); its mutex and published flags are internal statics, and chunk buffers, SHA context and tag re-reads use internal memory |
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
schema 0 uses a 3,344-byte blob for the bounded Micra cloud account, selected
machine, and per-scale friendly names. Blobs from earlier schema epochs are
rejected. The connection byte uses prior padding; the checksum stays at offset
3336. Trial-boot writes wait for OTA confirmation to retain rollback settings.

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
The startup heap hold currently requests 53,248 bytes (52 KiB) as a comparison
candidate; the same mechanism supports the previous 45,056-byte (44 KiB) and
57,344-byte (56 KiB) trials, 49,152 bytes (48 KiB), the original 60,000-byte
baseline and zero.
Zero skips allocation and records `disabled`,
then starts the 60-second post-release window immediately. This candidate and
the reduced WS task stack require target qualification; they are not an optimum
established by the earlier captures.
It also samples `INTERNAL|DMA`; the overlapping capability pools are not additive.
First-stage samples and approximately 100 ms health-task samples maintain separate
hold/post-release minima plus an independent 60-second minimum from the first
WS outcome, without resetting lifetime watermarks. Client-init and task-start
stages expose the setup allocation phases. Sampling stops when the release
and WS windows finish; a WS start without an outcome is bounded to 60 seconds
from that first start. Records remain available through
[`BOOT_HEAP`](SERIAL_CLI.md#capture-startup-memory) until reset. These sampled
minima can miss shorter dips; missing phases/stages are explicit in the report.
Diagnostic `memoryAllocations` reports cumulative
successes, failures, largest requested size, and last failed size by owner for
the application's capability-allocation wrappers. These counters are not live
allocation counts and do not include allocations made directly by SDK code.
The retained legacy external-fallback counter stays zero: the application's
explicit external allocator has no fallback. It does not measure ordinary
SDK malloc fallback or prove that all SDK allocations landed in PSRAM.
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
Heap alerts use raw internal free bytes and the largest block. A valid zero
is exhaustion, including a zero largest block. Invalid or stale samples cannot
clear the heap latch or request a restart; a fresh valid sample is needed.
The existing hysteresis, five-minute sustained-low window and configuration-safe,
open-circuit restart admission remain unchanged. Shaper-held memory is unavailable
until released and is never added to these health readings.
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

The current n16r8 Micra development+JTAG build measures 177,434 linked DIRAM bytes.
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
the evidence. Prefer the public debug export (it carries the same health and
heap telemetry the retired status page served) when the Diagnostic page is
enabled:

```sh
python3 scripts/p2_soak.py \
  --url http://192.168.1.50/api/v1/debug/export \
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
