#pragma once

#include "ShotStopperPsram.h"
#include "ShotStopperTaskMutex.h"

namespace shotstopper {

enum class BootHeapStage : uint8_t {
  RESERVE_BEFORE, RESERVE_AFTER, WIFI_INIT, MDNS_READY, HTTP_READY,
  NTP_DONE, CLOUD_DONE, RELEASE_BEFORE, RELEASE_AFTER, WS_START, WS_DONE,
  COUNT
};
enum class BootHeapResult : uint8_t { OK, FAILED, SETTLED, TIMEOUT, STOP, NO_RESERVATION };
constexpr uint32_t BOOT_HEAP_POST_RELEASE_MS = 60000;

inline const char *bootHeapStageName(BootHeapStage stage) {
  constexpr const char *names[] = {
      "reserve_before", "reserve_after", "wifi_init", "mdns_ready",
      "http_ready", "ntp_done", "cloud_done", "release_before",
      "release_after", "ws_start", "ws_done"};
  return names[static_cast<size_t>(stage)];
}
inline const char *bootHeapResultName(BootHeapResult result) {
  constexpr const char *names[] = {"ok", "failed", "settled", "timeout", "stop", "disabled"};
  return names[static_cast<size_t>(result)];
}

struct BootHeapRecord {
  uint32_t atMs = 0;
  HeapLifecycleSample internal{};
  uint32_t dmaFree = 0;
  uint32_t dmaLargest = 0;
  uint32_t psramFree = 0;
  uint32_t psramLargest = 0;
  uint32_t heldBytes = 0;
  BootHeapResult result{};
};
struct BootHeapMinimum {
  uint32_t samples = 0;
  uint32_t free = 0;
  uint32_t largest = 0;
  uint32_t dmaFree = 0;
  uint32_t dmaLargest = 0;
};
struct BootHeapCapture {
  BootHeapRecord records[static_cast<size_t>(BootHeapStage::COUNT)]{};
  BootHeapMinimum held{}, post{};
  uint32_t stages = 0;
  uint32_t heldBytes = 0;
  uint32_t requestedBytes = 0;
  uintptr_t address = 0;
  bool complete = false;
};
static_assert(sizeof(BootHeapCapture) <= 768, "Boot heap capture exceeds its PSRAM budget");

// One capture owner, shared by task-only producers and the serial reader.
// Synchronization stays internal; retained payload never consumes heap DRAM.
inline TaskMutex bootHeapMutex;
inline SHOT_STOPPER_PSRAM_BSS BootHeapCapture bootHeapCapture;
inline std::atomic<bool> bootHeapSampling{false};

inline BootHeapRecord sampleBootHeap(uint32_t now, const HeapCapSnapshot &heap) {
  BootHeapRecord record;
  record.atMs = now;
  record.internal = heapLifecycleSample(heap);
  record.psramFree = heap.psramFree;
  record.psramLargest = heap.psramLargest;
#if !defined(SHOT_STOPPER_HOST_TEST) && !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  multi_heap_info_t dma{};
  heap_caps_get_info(&dma, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  record.dmaFree = static_cast<uint32_t>(dma.total_free_bytes);
  record.dmaLargest = static_cast<uint32_t>(dma.largest_free_block);
#endif
  return record;
}

inline void updateBootHeapMinimum(BootHeapMinimum &minimum, const BootHeapRecord &record) {
  if (minimum.samples++ == 0) {
    minimum.free = record.internal.freeBytes;
    minimum.largest = record.internal.largestBlock;
    minimum.dmaFree = record.dmaFree;
    minimum.dmaLargest = record.dmaLargest;
    return;
  }
  if (record.internal.freeBytes < minimum.free) minimum.free = record.internal.freeBytes;
  if (record.internal.largestBlock < minimum.largest) minimum.largest = record.internal.largestBlock;
  if (record.dmaFree < minimum.dmaFree) minimum.dmaFree = record.dmaFree;
  if (record.dmaLargest < minimum.dmaLargest) minimum.dmaLargest = record.dmaLargest;
}

inline void recordBootHeap(BootHeapStage stage, BootHeapResult result, uint32_t now,
                           const HeapCapSnapshot &heap, uintptr_t address = 0,
                           uint32_t requestedBytes = 0) {
  const uint32_t bit = 1U << static_cast<unsigned>(stage);
  {
    TaskLockGuard lock(bootHeapMutex);
    if (bootHeapCapture.complete || (bootHeapCapture.stages & bit)) return;
    if (stage != BootHeapStage::RESERVE_BEFORE && bootHeapCapture.stages == 0) return;
  }
  BootHeapRecord record = sampleBootHeap(now, heap);
  record.result = result;
  TaskLockGuard lock(bootHeapMutex);
  auto &capture = bootHeapCapture;
  if (capture.complete || (capture.stages & bit)) return;
  if (stage == BootHeapStage::RESERVE_AFTER) {
    capture.requestedBytes = requestedBytes;
    capture.address = address;
    capture.heldBytes = address ? requestedBytes : 0;
  } else if (stage == BootHeapStage::RELEASE_AFTER) {
    capture.heldBytes = 0;
  }
  record.heldBytes = capture.heldBytes;
  capture.records[static_cast<size_t>(stage)] = record;
  capture.stages |= bit;
  bootHeapSampling.store(true, std::memory_order_release);
  if (record.heldBytes) updateBootHeapMinimum(capture.held, record);
  else if (capture.stages & (1U << static_cast<unsigned>(BootHeapStage::RELEASE_AFTER)))
    updateBootHeapMinimum(capture.post, record);
}

inline void recordBootHeap(BootHeapStage stage, BootHeapResult result, uint32_t now) {
  // Avoid sampling on every reconnection after the one-shot stage is retained.
  {
    TaskLockGuard lock(bootHeapMutex);
    if (bootHeapCapture.complete || bootHeapCapture.stages == 0 ||
        (bootHeapCapture.stages & (1U << static_cast<unsigned>(stage)))) return;
  }
  recordBootHeap(stage, result, now, sampleHeapCaps());
}

inline void serviceBootHeap(uint32_t now) {
  if (!bootHeapSampling.load(std::memory_order_acquire)) return;
  uint32_t heldBytes;
  {
    TaskLockGuard lock(bootHeapMutex);
    auto &capture = bootHeapCapture;
    if (capture.complete || capture.stages == 0) return;
    const auto &released = capture.records[static_cast<size_t>(BootHeapStage::RELEASE_AFTER)];
    if ((capture.stages & (1U << static_cast<unsigned>(BootHeapStage::RELEASE_AFTER))) &&
        static_cast<uint32_t>(now - released.atMs) >= BOOT_HEAP_POST_RELEASE_MS) {
      capture.complete = true;
      bootHeapSampling.store(false, std::memory_order_release);
      return;
    }
    heldBytes = capture.heldBytes;
  }
  const BootHeapRecord record = sampleBootHeap(now, sampleHeapCaps());
  TaskLockGuard lock(bootHeapMutex);
  auto &capture = bootHeapCapture;
  // A sample overlapping release cannot be attributed to either phase.
  if (capture.complete || heldBytes != capture.heldBytes) return;
  if (heldBytes) updateBootHeapMinimum(capture.held, record);
  else if (capture.stages & (1U << static_cast<unsigned>(BootHeapStage::RELEASE_AFTER)))
    updateBootHeapMinimum(capture.post, record);
}

}  // namespace shotstopper
