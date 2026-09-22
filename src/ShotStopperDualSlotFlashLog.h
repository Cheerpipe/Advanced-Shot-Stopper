#pragma once

// Shared machinery for the deferred dual-slot flash ring stores (ShotLog,
// ShotCurveLog, HistoryLog): PSRAM working copy, generation flip through the
// inactive slot, chunked internal-SRAM-staged transfers while the flash cache
// is disabled, and the host-test double for deterministic persistence tests.
// Each store header specializes DualSlotFlashLogTraits (partition name, slot
// geometry, and the valid/compact/finalize/reset hooks) and keeps its own
// record-level API. Flash partitions, slot sizes, and on-disk schemas are
// owned by the store headers and stay byte-identical.

#include "ShotStopperFlashIoScratch.h"
#include "ShotStopperNvsDualSlot.h"

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <esp_partition.h>
#endif

namespace shotstopper {

template <typename StoreT>
struct DualSlotFlashLogTraits;

template <typename StoreT, bool (*ValidFn)(const StoreT &),
          void (*CompactFn)(StoreT &), void (*FinalizeFn)(StoreT &)>
class DualSlotFlashLog {
 public:
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  static void resetHostStorage() {
    memset(hostSlots_, 0, sizeof(hostSlots_));
    hostSlotValid_[0] = false;
    hostSlotValid_[1] = false;
    hostSaveSucceeds_ = true;
  }
  static void setHostSaveSucceeds(bool succeeds) { hostSaveSucceeds_ = succeeds; }
#endif

  bool load() {
    using Traits = DualSlotFlashLogTraits<StoreT>;
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    uint8_t bestSlot = 0;
    bool haveBest = false;
    for (uint8_t slot = 0; slot < 2; ++slot) {
      if (!hostSlotValid_[slot] || !ValidFn(hostSlots_[slot])) {
        continue;
      }
      if (!haveBest || secondRevisionIsNewer(hostSlots_[bestSlot].header.generation,
                                             hostSlots_[slot].header.generation)) {
        bestSlot = slot;
        haveBest = true;
      }
    }
    if (haveBest) {
      memcpy(&store_, &hostSlots_[bestSlot], sizeof(store_));
      activeSlot_ = bestSlot;
    } else {
      Traits::reset(store_);
      activeSlot_ = 0;
    }
    dirty_ = false;
    return true;
#else
    if (!lockFlashIo()) {
      Traits::reset(store_);
      dirty_ = false;
      return false;
    }
    const esp_partition_t *part = flashLogPartition();
    if (part == nullptr ||
        part->size < Traits::kSlotCount * Traits::kSlotBytes) {
      Traits::reset(store_);
      dirty_ = false;
      unlockFlashIo();
      return false;
    }

    const bool aOk = readSlot(part, 0, store_) && ValidFn(store_);
    const uint32_t gen0 = aOk ? store_.header.generation : 0;
    const bool bOk =
        readSlot(part, Traits::kSlotBytes, store_) && ValidFn(store_);
    const DualSlotChoice choice =
        chooseNewerRevision(aOk, gen0, bOk,
                            bOk ? store_.header.generation : 0);
    if (choice == DualSlotChoice::SECOND) {
      // store_ already holds slot B.
      activeSlot_ = 1;
    } else if (choice == DualSlotChoice::FIRST) {
      // store_ holds slot B or a failed slot-B read; restore the winner.
      (void)readSlot(part, 0, store_);
      activeSlot_ = 0;
    } else {
      Traits::reset(store_);
      activeSlot_ = 0;
    }
    dirty_ = false;
    unlockFlashIo();
    return true;
#endif
  }

  bool save(uint32_t lockTimeoutMs = FLASH_IO_LOCK_TIMEOUT_MS) {
    if (!tryLockFlashIo(lockTimeoutMs)) {
      return false;
    }
    CompactFn(store_);
    if (store_.header.generation == 0) {
      store_.header.generation = 1;
    } else if (store_.header.generation < UINT32_MAX) {
      ++store_.header.generation;
    }
    FinalizeFn(store_);
    const uint8_t targetSlot = static_cast<uint8_t>(1U - (activeSlot_ & 1U));
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    if (!hostSaveSucceeds_) {
      unlockFlashIo();
      return false;
    }
    memcpy(&hostSlots_[targetSlot], &store_, sizeof(store_));
    hostSlotValid_[targetSlot] = true;
    activeSlot_ = targetSlot;
    dirty_ = false;
    unlockFlashIo();
    return true;
#else
    using Traits = DualSlotFlashLogTraits<StoreT>;
    const esp_partition_t *part = flashLogPartition();
    if (part == nullptr ||
        part->size < Traits::kSlotCount * Traits::kSlotBytes) {
      unlockFlashIo();
      return false;
    }
    yieldFlashIo();
    feedFlashIoWatchdog();
    const size_t targetOffset =
        static_cast<size_t>(targetSlot) * Traits::kSlotBytes;
    if (esp_partition_erase_range(part, targetOffset,
                                  Traits::kSlotBytes) != ESP_OK) {
      unlockFlashIo();
      return false;
    }
    yieldFlashIo();
    feedFlashIoWatchdog();
    // Chunked write: each 1 KiB step stages through the internal scratch
    // because the live store_ sits in PSRAM BSS, unreachable while the flash
    // cache is disabled inside the partition call.
    if (!flashIoWriteChunked(part, targetOffset, &store_, sizeof(store_))) {
      unlockFlashIo();
      return false;
    }
    activeSlot_ = targetSlot;
    dirty_ = false;
    unlockFlashIo();
    return true;
#endif
  }

  bool flush(uint32_t lockTimeoutMs = FLASH_IO_CONTROL_LOCK_TIMEOUT_MS) {
    if (!dirty_) {
      return true;
    }
    return save(lockTimeoutMs);
  }

  FlashStoreStepResult flushStep(
      uint32_t lockTimeoutMs = FLASH_IO_LOCK_TIMEOUT_MS) {
    if (!dirty_) return FlashStoreStepResult::COMPLETE;
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    return save(lockTimeoutMs) ? FlashStoreStepResult::COMPLETE
                               : FlashStoreStepResult::FAILED;
#else
    using Traits = DualSlotFlashLogTraits<StoreT>;
    const esp_partition_t *part = flashLogPartition();
    if (part == nullptr ||
        part->size < Traits::kSlotCount * Traits::kSlotBytes) {
      return FlashStoreStepResult::FAILED;
    }
    if (persistProgress_.phase == FlashStoreTransaction::Phase::IDLE) {
      CompactFn(store_);
      if (store_.header.generation == 0) store_.header.generation = 1;
      else if (store_.header.generation < UINT32_MAX) ++store_.header.generation;
      FinalizeFn(store_);
      beginFlashStoreTransaction(
          persistProgress_,
          static_cast<size_t>(1U - (activeSlot_ & 1U)) * Traits::kSlotBytes,
          Traits::kSlotBytes, Traits::kHeaderBytes, sizeof(store_));
    }
    const FlashStoreStepResult result = flashIoStoreStep(
        part, &store_, persistProgress_, lockTimeoutMs);
    if (result == FlashStoreStepResult::COMPLETE) {
      activeSlot_ = static_cast<uint8_t>(1U - (activeSlot_ & 1U));
      dirty_ = false;
    }
    return result;
#endif
  }

  bool dirty() const { return dirty_; }

 protected:
#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  static const esp_partition_t *flashLogPartition() {
    return esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40),
        DualSlotFlashLogTraits<StoreT>::kPartitionName);
  }

  static bool readSlot(const esp_partition_t *part, size_t offset,
                       StoreT &dest) {
    memset(&dest, 0, sizeof(dest));
    if (part == nullptr) {
      return false;
    }
    return flashIoReadChunked(part, offset, &dest, sizeof(dest));
  }
#endif

  StoreT store_{};
  FlashStoreTransaction persistProgress_{};
  uint8_t activeSlot_ = 0;
  bool dirty_ = false;

#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  inline static StoreT hostSlots_[2] = {};
  inline static bool hostSlotValid_[2] = {};
  inline static bool hostSaveSucceeds_ = true;
#endif
};

}  // namespace shotstopper
