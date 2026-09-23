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
  using Traits = DualSlotFlashLogTraits<StoreT>;
  using Record = typename Traits::Record;

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
    const bool bOk = readSlot(part, Traits::kSlotBytes, store_) &&
                     ValidFn(store_);
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

  // Record-level ring API shared by the stores. The per-store traits supply
  // the record type, ring capacity, and the record field that carries the
  // record id (ShotLog/History use `id`, ShotCurve uses `shotId`).
  const Record *findNewestById(uint32_t id) const {
    if (id == 0 || store_.header.count == 0) {
      return nullptr;
    }
    size_t index = store_.header.writeIndex;
    for (size_t n = 0; n < store_.header.count; ++n) {
      if (index == 0) {
        index = Traits::kCapacity;
      }
      --index;
      if (Traits::recordIdOf(store_.records[index]) == id) {
        return &store_.records[index];
      }
    }
    return nullptr;
  }

  Record *findNewestById(uint32_t id) {
    return const_cast<Record *>(
        static_cast<const DualSlotFlashLog *>(this)->findNewestById(id));
  }

  bool containsId(uint32_t id) const { return findNewestById(id) != nullptr; }

  size_t copyNewestFirst(Record *output, size_t capacity) const {
    if (output == nullptr || capacity == 0 || store_.header.count == 0) {
      return 0;
    }
    const size_t toCopy =
        store_.header.count < capacity ? store_.header.count : capacity;
    size_t index = store_.header.writeIndex;
    for (size_t copied = 0; copied < toCopy; ++copied) {
      if (index == 0) {
        index = Traits::kCapacity;
      }
      --index;
      output[copied] = store_.records[index];
    }
    return toCopy;
  }

  // Assigns the next record id, advances the ring, and persists. On a failed
  // immediate save the store is rolled back when the failure was a lock
  // timeout, or reloaded from the last-good slot otherwise (compaction may
  // have moved records, making the pre-append snapshot stale).
  bool append(const Record &record, bool persistNow = true) {
    const uint32_t lockTimeoutsBefore = flashIoLockTimeouts();
    const uint16_t previousWriteIndex = store_.header.writeIndex;
    const uint16_t previousCount = store_.header.count;
    const uint32_t previousNextRecordId = store_.header.nextRecordId;
    const Record overwritten = store_.records[previousWriteIndex];

    Record stored = record;
    stored.id = store_.header.nextRecordId;
    if (store_.header.nextRecordId < UINT32_MAX) {
      ++store_.header.nextRecordId;
    }
    store_.records[store_.header.writeIndex] = stored;
    store_.header.writeIndex =
        static_cast<uint16_t>((store_.header.writeIndex + 1U) %
                              Traits::kCapacity);
    if (store_.header.count < Traits::kCapacity) {
      ++store_.header.count;
    }
    if (!persistNow) {
      dirty_ = true;
      return true;
    }
    if (save()) {
      return true;
    }
    if (flashIoLockTimeouts() == lockTimeoutsBefore) {
      load();
      return false;
    }
    store_.records[previousWriteIndex] = overwritten;
    store_.header.writeIndex = previousWriteIndex;
    store_.header.count = previousCount;
    store_.header.nextRecordId = previousNextRecordId;
    return false;
  }

  // Compacts to a linear prefix so deletion is a memmove, avoiding a second
  // capacity-sized record array on the loopTask stack.
  bool removeById(uint32_t id, bool persistNow = true) {
    if (id == 0 || store_.header.count == 0) {
      return false;
    }
    if (persistNow && !lockFlashIo()) {
      return false;
    }
    CompactFn(store_);
    bool found = false;
    size_t foundIndex = 0;
    for (size_t index = 0; index < store_.header.count; ++index) {
      if (Traits::recordIdOf(store_.records[index]) == id) {
        found = true;
        foundIndex = index;
        break;
      }
    }
    if (!found) {
      if (persistNow) unlockFlashIo();
      return false;
    }
    const uint16_t previousCount = store_.header.count;
    if (foundIndex + 1U < previousCount) {
      memmove(&store_.records[foundIndex], &store_.records[foundIndex + 1U],
              static_cast<size_t>(previousCount - foundIndex - 1U) *
                  sizeof(Record));
    }
    --store_.header.count;
    store_.header.writeIndex =
        static_cast<uint16_t>(store_.header.count % Traits::kCapacity);
    memset(&store_.records[store_.header.count], 0, sizeof(Record));
    if (!persistNow) {
      dirty_ = true;
      return true;
    }
    const bool saved = save();
    unlockFlashIo();
    if (saved) {
      return true;
    }
    load();
    return false;
  }

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
