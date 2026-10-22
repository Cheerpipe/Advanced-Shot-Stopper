# ESP-IDF memory watch

Developer reference. This file tracks the ESP-IDF behaviours that shape how the
firmware allocates and measures internal RAM, the workaround in place for each,
and the change to make when ESP-IDF changes. Review it on every ESP-IDF upgrade
and whenever internal RAM becomes a constraint. Status was last checked on
2026-10-22 against ESP-IDF v6.1 (release/v6.1) and master; there was no
release/v6.2 branch yet.

To see the effect on a running controller, download **Diagnostic → HEAP → Heap
map** (internal block map per startup stage) and read the `bootHeap` section of
the debug export. Both work on release images; USB is not needed.

## 1. Allocation priority sends the Wi-Fi bring-up wave into the main DRAM

**What happens.** The ESP32-S3 heap tries memory by capability priority, not by
size or speed. The main DIRAM is high priority for `INTERNAL`, `DMA` and
`DEFAULT` requests. The 32 KB internal reserve pool
(`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`) and the 32 KB DRAM shared with the
data cache (`0x3FCF0000`, present only with a 32 KB data cache) are lower
priority fallbacks. All internal SRAM has the same access time; the order
protects capabilities (DMA retention, executable memory) and keeps the reserve
for emergencies. The ~33 KB of small allocations made while Wi-Fi, lwIP, the
supplicant, mDNS and the HTTP server start therefore land in the main DRAM and
split its central free run into two blocks of about 30 KB each.

**Assessment.** Not a fault. Nothing in the firmware needs more than ~11 KB of
contiguous internal RAM (the largest runtime task stack); the health monitor
treats a largest block of 24 KB or more as healthy. The fallbacks staying
intact is the intended design. The former startup heap hold, which forced the
wave into the fallbacks, was removed on 2026-10-22 (see
[resource budgets](P2_RESOURCE_BUDGETS.md)).

**Upstream.**
- [esp-idf#13588](https://github.com/espressif/esp-idf/issues/13588) (closed):
  heaps are sorted by ascending size since v5.5; staff suggest
  `heap_caps_add_region_with_caps()` to steer allocations. Priority order is
  unchanged.
- [esp-idf#11753](https://github.com/espressif/esp-idf/issues/11753) (open since
  2023): Espressif promised to optimise the Wi-Fi and lwIP allocation strategy.
  Nothing has shipped.
- No release, pull request or master commit changes the S3 priority order, the
  memory layout or the Wi-Fi allocation paths.

**Watch for.** A change to `heap_caps_base.c` priority handling, the S3
`memory_layout.c`, `esp_wifi/esp32s3/esp_adapter.c` (`wifi_malloc`, the
internal allocation wrappers) or the lwIP `mem_clib_malloc` strategy.

**If internal RAM becomes the constraint, levers in order.**
1. Disable `CONFIG_ESP_WIFI_IRAM_OPT` and `CONFIG_ESP_WIFI_RX_IRAM_OPT`: about
   18 KB back for +0.3 % of a core in the Wi-Fi task. Reviewed, measured and
   accepted; see [resource budgets](P2_RESOURCE_BUDGETS.md).
2. Lower `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` (now 2048): small `malloc()`
   requests, including most of the Wi-Fi wave, would go to PSRAM first. Affects
   the whole system; measure with the heap map and a multi-hour soak.
3. A dedicated Wi-Fi arena: carve a block early, register it with
   `heap_caps_add_region_with_caps()` under a capability only redirected
   requests carry, and point the internal allocation hooks of the public,
   mutable `g_wifi_osi_funcs` at it before Wi-Fi starts. Captures only the
   Wi-Fi library's own allocations (not the supplicant, events or lwIP), and the
   arena can never be removed. Use only if a large contiguous block becomes a
   real requirement.

Do not patch ESP-IDF itself for this: the hooks above make it unnecessary, and
the project has no mechanism to carry framework patches across upgrades.

## 2. The reserve pool documentation contradicts the code

**What happens.** The ESP-IDF external RAM guide says explicit
`MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL` requests are "preferentially served"
from the reserve pool. On the S3 the code serves them from the main DRAM first,
because DIRAM matches those capabilities at the higher priority. The text came
with commit 38421b8db7 (v6.1, also v6.0.3 and v5.5.5).

**Upstream.** Not reported. The closest issue,
[esp-idf#18938](https://github.com/espressif/esp-idf/issues/18938) (open),
covers the opposite symptom (`malloc()` draining the pool); Espressif said a
fix "would require additional allocator logic".

**Watch for.** A docs correction or allocator change around the reserve pool.
If the pool ever becomes preferred for explicit DMA/internal requests, the
Wi-Fi internal allocations would start consuming it: re-check the heap map
after such an upgrade. Optional: report the S3 mismatch upstream.

## 3. The lifetime heap minimum under-reports by the reserve pool

**What happens.** In ESP-IDF v6.1, `heap_caps_get_minimum_free_size()` for
internal memory reports about 32 KB less than the real minimum, because the
reserve pool is carved out of the main heap and registered after the startup
phase. A controller with about 74 KB of real minimum showed 39.5 KB.

**Workaround in place.** The network service starts ESP-IDF local minimum
monitoring (`heap_caps_monitor_local_minimum_free_size_start()`) at network
start, before Wi-Fi bring-up (`src/network/OpenBrewByWeightWifi.inc`). The
Diagnostic Heap min, health, `HEAP` and the debug export therefore report the
minimum since network start. Heap alerts use current free memory and the
largest block, never this minimum.

**Upstream.** [esp-idf#19261](https://github.com/espressif/esp-idf/issues/19261)
(open, assigned 2026-10-21, no fix yet).

**When fixed.** Keep the workaround: "minimum since network start" stays a
meaningful, honest figure and excludes one-time boot transients. Only remove
the esp-idf#19261 reference from its comment.

## 4. Planned default change for small allocations

**What happens.** Espressif plans to change the default of
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` to 0 (from 16384) as the fix for
esp-idf#18938. Nothing is merged yet.

**Impact here.** None by itself: the project sets 2048 explicitly in
`idf/sdkconfig.defaults`. If Espressif also changes how the reserve pool is
skipped, re-check the heap map and the boot heap capture after the upgrade.
