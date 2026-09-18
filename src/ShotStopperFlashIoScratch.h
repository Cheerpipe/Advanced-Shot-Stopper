#pragma once

// Shared internal-SRAM workspace for Preferences getBytes/putBytes and chunked
// partition transfers while the flash cache may be disabled (PSRAM is then
// inaccessible on ESP32-S3). Settings dual-slot I/O, the last-shot blob, and
// the per-chunk staging bytes reuse the same allocation; every path must hold
// tryLockFlashIo() for the whole use of the scratch.

#include "ShotStopperDomain.h"
#include "ShotStopperPsram.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <esp_memory_utils.h>
#include <esp_partition.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

namespace shotstopper {

// Floor set by the settings dual-slot staging (2 × sizeof(PersistedSettings);
// authoritative static_asserts live in persistedSettingsScratch() and
// LastShotStore) plus the chunk staging below. The larger partition stores
// (shotlog, history, shotcurve) never fit here: they transfer in
// FLASH_IO_CHUNK_BYTES steps through flashIoReadChunked/flashIoWriteChunked.
constexpr size_t FLASH_IO_SCRATCH_BYTES = 5296;
// One staged step of a partition-store transfer. 4-byte alignment keeps every
// esp_partition_read/write call word-aligned; store sizes are multiples of 4.
constexpr size_t FLASH_IO_CHUNK_BYTES = 1024;
static_assert(FLASH_IO_CHUNK_BYTES % 4 == 0,
              "Flash I/O chunks must stay 4-byte aligned");
static_assert(FLASH_IO_CHUNK_BYTES <= FLASH_IO_SCRATCH_BYTES,
              "Flash I/O chunks must stage through the scratch");
constexpr uint32_t FLASH_IO_LOCK_TIMEOUT_MS = 3000;
// Control-loop NVS must fail fast; the durable timeout itself retains two
// seconds of margin beneath the task watchdog budget.
constexpr uint32_t FLASH_IO_CONTROL_LOCK_TIMEOUT_MS = 50;

static_assert(FLASH_IO_CONTROL_LOCK_TIMEOUT_MS < FLASH_IO_LOCK_TIMEOUT_MS,
              "Control flash lock must be shorter than durable I/O");

inline uint8_t *&flashIoScratchBlock() {
  static uint8_t *block = nullptr;
  return block;
}

// Internal SRAM, heap-accounted. Do not use BSS: ALLOW_BSS would put this block in
// PSRAM (SET_WIFI load fails), and a forced .dram0.bss object can overlap the
// heap (IWDT on a corrupted malloc spinlock, magic 0x53544F50 / "STOP").
inline uint8_t *flashIoScratchBytes() {
  return flashIoScratchBlock();
}

inline bool ensureFlashIoScratch() {
  uint8_t *&block = flashIoScratchBlock();
  if (block != nullptr) {
    return true;
  }
  block = static_cast<uint8_t *>(allocInternal(FLASH_IO_SCRATCH_BYTES, AllocationOwner::FLASH_IO));
  if (block == nullptr) {
    return false;
  }
#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  if (!esp_ptr_internal(block)) {
    heapCapsFree(block);
    block = nullptr;
    return false;
  }
#endif
  memset(block, 0, FLASH_IO_SCRATCH_BYTES);
  return true;
}

inline std::atomic<uint32_t> &flashIoLockTimeoutCount() {
  static std::atomic<uint32_t> count{0};
  return count;
}

inline uint32_t flashIoLockTimeouts() {
  return flashIoLockTimeoutCount().load(std::memory_order_relaxed);
}

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
inline SemaphoreHandle_t &flashIoMutexSlot() {
  static SemaphoreHandle_t handle = nullptr;
  return handle;
}

inline bool ensureFlashIoMutex() {
  SemaphoreHandle_t &handle = flashIoMutexSlot();
  if (handle != nullptr) {
    return true;
  }
  handle = xSemaphoreCreateRecursiveMutex();
  return handle != nullptr;
}

inline SemaphoreHandle_t flashIoMutexHandle() { return flashIoMutexSlot(); }

inline bool tryLockFlashIo(uint32_t timeoutMs = FLASH_IO_LOCK_TIMEOUT_MS) {
  if (!ensureFlashIoMutex()) {
    return false;
  }
  SemaphoreHandle_t handle = flashIoMutexHandle();
  if (xSemaphoreTakeRecursive(handle, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
    if (!ensureFlashIoScratch()) {
      xSemaphoreGiveRecursive(handle);
      return false;
    }
    return true;
  }
  flashIoLockTimeoutCount().fetch_add(1, std::memory_order_relaxed);
  return false;
}

inline void unlockFlashIo() {
  SemaphoreHandle_t handle = flashIoMutexHandle();
  if (handle != nullptr) {
    xSemaphoreGiveRecursive(handle);
  }
}
#else
inline bool g_hostFlashIoMutexAvailable = true;

inline bool ensureFlashIoMutex() { return g_hostFlashIoMutexAvailable; }

inline bool tryLockFlashIo(uint32_t = FLASH_IO_LOCK_TIMEOUT_MS) {
  if (!g_hostFlashIoMutexAvailable) {
    flashIoLockTimeoutCount().fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return ensureFlashIoScratch();
}
inline void unlockFlashIo() {}
#endif

inline void yieldFlashIo() {
#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  vTaskDelay(pdMS_TO_TICKS(1));
#endif
}

inline void feedFlashIoWatchdog() {
#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  // HTTP workers are not TWDT subscribers. Resetting from them logs
  // "esp_task_wdt_reset: task not found" and does not feed loop/network/scale.
  if (esp_task_wdt_status(nullptr) == ESP_OK) {
    (void)esp_task_wdt_reset();
  }
#endif
}

// Compatibility alias used by existing call sites.
inline bool lockFlashIo() { return tryLockFlashIo(); }
inline bool lockFlashIoForControl() {
  return tryLockFlashIo(FLASH_IO_CONTROL_LOCK_TIMEOUT_MS);
}

// Preferences putBytes/getBytes must not touch PSRAM while flash cache is
// off. Caller must already hold the flash I/O lock so the scratch exists.
inline void *copyToFlashIoScratch(const void *source, size_t bytes) {
  if (source == nullptr || bytes == 0 || bytes > FLASH_IO_SCRATCH_BYTES) {
    return nullptr;
  }
  uint8_t *scratch = flashIoScratchBytes();
  if (scratch == nullptr) {
    return nullptr;
  }
  memcpy(scratch, source, bytes);
  return scratch;
}

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
// Chunked partition transfer for PSRAM-backed stores larger than the scratch.
// Each step copies one chunk between PSRAM and the internal scratch while the
// flash cache is on; only the internal staging bytes are reachable inside the
// partition call while it is off. Caller must already hold the flash I/O lock
// for the whole transfer.
inline bool flashIoReadChunked(const esp_partition_t *part, size_t offset,
                               void *destination, size_t bytes) {
  uint8_t *scratch = flashIoScratchBytes();
  if (part == nullptr || destination == nullptr || scratch == nullptr) {
    return false;
  }
  uint8_t *out = static_cast<uint8_t *>(destination);
  for (size_t done = 0; done < bytes;) {
    const size_t chunk = bytes - done < FLASH_IO_CHUNK_BYTES
                             ? bytes - done
                             : FLASH_IO_CHUNK_BYTES;
    if (esp_partition_read(part, offset + done, scratch, chunk) != ESP_OK) {
      return false;
    }
    memcpy(out + done, scratch, chunk);
    done += chunk;
    yieldFlashIo();
    feedFlashIoWatchdog();
  }
  return true;
}

inline bool flashIoWriteChunked(const esp_partition_t *part, size_t offset,
                                const void *source, size_t bytes) {
  uint8_t *scratch = flashIoScratchBytes();
  if (part == nullptr || source == nullptr || scratch == nullptr) {
    return false;
  }
  const uint8_t *in = static_cast<const uint8_t *>(source);
  for (size_t done = 0; done < bytes;) {
    const size_t chunk = bytes - done < FLASH_IO_CHUNK_BYTES
                             ? bytes - done
                             : FLASH_IO_CHUNK_BYTES;
    memcpy(scratch, in + done, chunk);
    if (esp_partition_write(part, offset + done, scratch, chunk) != ESP_OK) {
      return false;
    }
    done += chunk;
    yieldFlashIo();
    feedFlashIoWatchdog();
  }
  return true;
}
#endif

}  // namespace shotstopper
