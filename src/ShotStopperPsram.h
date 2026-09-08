#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <atomic>

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <esp_attr.h>
#endif

// Place large BSS in PSRAM on the official IDF build
// (CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y). Arduino-cli cores that ship
// that Kconfig off compile EXT_RAM_BSS_ATTR as empty, so the same objects stay
// in DRAM. Never mark flash DMA sources, OTA chunks, httpd bounce, or stacks of
// tasks that write flash: cache-off cannot reach PSRAM.
#if defined(SHOT_STOPPER_HOST_TEST) ||                                         \
    defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#define SHOT_STOPPER_PSRAM_BSS
#else
#define SHOT_STOPPER_PSRAM_BSS EXT_RAM_BSS_ATTR
#endif

namespace shotstopper {

enum class AllocationOwner : uint8_t {
  OTHER, NETWORK, WEBHOOK, COMPANION, PROFILER, FLASH_IO, OTA, BUZZER, JSON, COUNT
};

struct AllocationMetrics {
  std::atomic<uint32_t> successes{0};
  std::atomic<uint32_t> failures{0};
  std::atomic<uint32_t> largestRequestBytes{0};
  std::atomic<uint32_t> lastFailureBytes{0};
};

namespace detail {

inline std::atomic<uint32_t> g_allocExternalOk{0};
inline std::atomic<uint32_t> g_allocExternalFallback{0};
inline std::atomic<bool> g_workBufExternal{false};
inline AllocationMetrics g_allocations[static_cast<size_t>(AllocationOwner::COUNT)];
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
// Deterministic allocation-failure injection; never compiled into firmware.
inline std::atomic<int32_t> g_hostAllocationsUntilFailure{-1};
inline bool hostAllocationFails() {
  int32_t left = g_hostAllocationsUntilFailure.load(std::memory_order_relaxed);
  while (left >= 0) {
    if (left == 0) return true;
    if (g_hostAllocationsUntilFailure.compare_exchange_weak(
            left, left - 1, std::memory_order_relaxed)) return false;
  }
  return false;
}
#endif

}  // namespace detail

inline void noteAllocation(AllocationOwner owner, size_t bytes, bool success) {
  auto &metrics = detail::g_allocations[static_cast<size_t>(owner)];
  const uint32_t size = bytes > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(bytes);
  uint32_t largest = metrics.largestRequestBytes.load(std::memory_order_relaxed);
  while (size > largest && !metrics.largestRequestBytes.compare_exchange_weak(
             largest, size, std::memory_order_relaxed)) {}
  if (success) {
    metrics.successes.fetch_add(1, std::memory_order_relaxed);
  } else {
    metrics.failures.fetch_add(1, std::memory_order_relaxed);
    metrics.lastFailureBytes.store(size, std::memory_order_relaxed);
  }
}

inline uint32_t allocExternalOkCount() {
  return detail::g_allocExternalOk.load(std::memory_order_relaxed);
}

inline uint32_t allocExternalFallbackCount() {
  return detail::g_allocExternalFallback.load(std::memory_order_relaxed);
}

inline bool workBufIsExternal() {
  return detail::g_workBufExternal.load(std::memory_order_acquire);
}

inline void noteWorkBufExternal(bool isExternal) {
  detail::g_workBufExternal.store(isExternal, std::memory_order_release);
}

inline bool pointerIsExternal(const void *block) {
  if (block == nullptr) {
    return false;
  }
#if defined(SHOT_STOPPER_HOST_TEST) ||                                         \
    defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  return true;
#else
  return esp_ptr_external_ram(block);
#endif
}

// SPIRAM only. Large blobs that must not punch a hole in internal DRAM
// (NetworkWorkBuf, Wi-Fi AP records). Returns nullptr if SPIRAM cannot
// satisfy — callers fail closed.
inline void *allocExternal(size_t bytes, AllocationOwner owner = AllocationOwner::OTHER) {
  if (bytes == 0) {
    return nullptr;
  }
#if defined(SHOT_STOPPER_HOST_TEST) ||                                         \
    defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  void *block = detail::hostAllocationFails() ? nullptr : malloc(bytes);
  noteAllocation(owner, bytes, block != nullptr);
  if (block != nullptr) {
    detail::g_allocExternalOk.fetch_add(1, std::memory_order_relaxed);
  }
  return block;
#else
  void *block = nullptr;
#if defined(BOARD_HAS_PSRAM)
  if (psramFound()) {
    block = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
#endif
  if (block != nullptr && pointerIsExternal(block)) {
    noteAllocation(owner, bytes, true);
    detail::g_allocExternalOk.fetch_add(1, std::memory_order_relaxed);
    return block;
  }
  if (block != nullptr) {
    heap_caps_free(block);
  }
  noteAllocation(owner, bytes, false);
  return nullptr;
#endif
}

inline void *allocInternal(size_t bytes, AllocationOwner owner = AllocationOwner::OTHER) {
  if (bytes == 0) {
    return nullptr;
  }
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  void *block = detail::hostAllocationFails() ? nullptr : malloc(bytes);
#else
  void *block = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
  noteAllocation(owner, bytes, block != nullptr);
  return block;
}

inline void heapCapsFree(void *block) {
  if (block == nullptr) {
    return;
  }
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  free(block);
#else
  heap_caps_free(block);
#endif
}

struct HeapCapSnapshot {
  uint32_t internalTotal = 0;
  uint32_t internalFree = 0;
  uint32_t internalMinimum = 0;
  uint32_t internalLargest = 0;
  uint32_t psramTotal = 0;
  uint32_t psramFree = 0;
  uint32_t psramMinimum = 0;
  uint32_t psramLargest = 0;
};

inline HeapCapSnapshot sampleHeapCaps() {
  HeapCapSnapshot snap;
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  snap.internalTotal = 327680;
  snap.internalFree = 200000;
  snap.internalMinimum = 180000;
  snap.internalLargest = 100000;
#else
  const uint32_t internalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  snap.internalTotal =
      static_cast<uint32_t>(heap_caps_get_total_size(internalCaps));
  snap.internalFree =
      static_cast<uint32_t>(heap_caps_get_free_size(internalCaps));
  snap.internalMinimum =
      static_cast<uint32_t>(heap_caps_get_minimum_free_size(internalCaps));
  snap.internalLargest = static_cast<uint32_t>(
      heap_caps_get_largest_free_block(internalCaps));
#if defined(BOARD_HAS_PSRAM)
  if (psramFound()) {
    const uint32_t psramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    snap.psramTotal =
        static_cast<uint32_t>(heap_caps_get_total_size(psramCaps));
    snap.psramFree = static_cast<uint32_t>(heap_caps_get_free_size(psramCaps));
    snap.psramMinimum = static_cast<uint32_t>(heap_caps_get_minimum_free_size(psramCaps));
    snap.psramLargest = static_cast<uint32_t>(
        heap_caps_get_largest_free_block(psramCaps));
  }
#endif
#endif
  return snap;
}

}  // namespace shotstopper
