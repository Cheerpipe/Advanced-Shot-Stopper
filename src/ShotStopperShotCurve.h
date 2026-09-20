#pragma once

#include "ShotStopperFlashIoScratch.h"
#include "ShotStopperNvsDualSlot.h"
#include "ShotStopperShotCurveTypes.h"

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <esp_partition.h>
#endif

namespace shotstopper {

// Two 28 KiB dual-slots fill the dedicated shotcurve data partition.
constexpr size_t SHOT_CURVE_FLASH_SLOT_BYTES = 28672;
constexpr size_t SHOT_CURVE_FLASH_SLOT_COUNT = 2;

class ShotCurveLog {
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
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    uint8_t bestSlot = 0;
    bool haveBest = false;
    for (uint8_t slot = 0; slot < 2; ++slot) {
      if (!hostSlotValid_[slot] || !validShotCurveStore(hostSlots_[slot])) {
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
      resetShotCurveStore(store_);
      activeSlot_ = 0;
    }
    dirty_ = false;
    return true;
#else
    if (!lockFlashIo()) {
      resetShotCurveStore(store_);
      dirty_ = false;
      return false;
    }
    const esp_partition_t *part = curvePartition();
    if (part == nullptr ||
        part->size < SHOT_CURVE_FLASH_SLOT_COUNT * SHOT_CURVE_FLASH_SLOT_BYTES) {
      resetShotCurveStore(store_);
      dirty_ = false;
      unlockFlashIo();
      return false;
    }

    const bool aOk =
        readSlot(part, 0, store_) && validShotCurveStore(store_);
    const uint32_t gen0 = aOk ? store_.header.generation : 0;
    const bool bOk =
        readSlot(part, SHOT_CURVE_FLASH_SLOT_BYTES, store_) &&
        validShotCurveStore(store_);
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
      resetShotCurveStore(store_);
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
    compactShotCurveStore(store_);
    if (store_.header.generation == 0) {
      store_.header.generation = 1;
    } else if (store_.header.generation < UINT32_MAX) {
      ++store_.header.generation;
    }
    finalizeShotCurveStore(store_);
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
    const esp_partition_t *part = curvePartition();
    if (part == nullptr ||
        part->size < SHOT_CURVE_FLASH_SLOT_COUNT * SHOT_CURVE_FLASH_SLOT_BYTES) {
      unlockFlashIo();
      return false;
    }
    yieldFlashIo();
    feedFlashIoWatchdog();
    const size_t targetOffset =
        static_cast<size_t>(targetSlot) * SHOT_CURVE_FLASH_SLOT_BYTES;
    if (esp_partition_erase_range(part, targetOffset,
                                  SHOT_CURVE_FLASH_SLOT_BYTES) != ESP_OK) {
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

  bool append(const ShotCurveRecord &record, bool persistNow = true) {
    const uint32_t lockTimeoutsBefore = flashIoLockTimeouts();
    const uint16_t previousWriteIndex = store_.header.writeIndex;
    const uint16_t previousCount = store_.header.count;
    const ShotCurveRecord overwritten = store_.records[previousWriteIndex];

    store_.records[store_.header.writeIndex] = record;
    store_.header.writeIndex = static_cast<uint16_t>(
        (store_.header.writeIndex + 1U) % SHOT_CURVE_CAPACITY);
    if (store_.header.count < SHOT_CURVE_CAPACITY) {
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
      (void)load();
      return false;
    }
    store_.records[previousWriteIndex] = overwritten;
    store_.header.writeIndex = previousWriteIndex;
    store_.header.count = previousCount;
    return false;
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
    const esp_partition_t *part = curvePartition();
    if (part == nullptr ||
        part->size < SHOT_CURVE_FLASH_SLOT_COUNT * SHOT_CURVE_FLASH_SLOT_BYTES) {
      return FlashStoreStepResult::FAILED;
    }
    if (persistProgress_.phase == FlashStoreTransaction::Phase::IDLE) {
      compactShotCurveStore(store_);
      if (store_.header.generation == 0) store_.header.generation = 1;
      else if (store_.header.generation < UINT32_MAX) ++store_.header.generation;
      finalizeShotCurveStore(store_);
      beginFlashStoreTransaction(
          persistProgress_,
          static_cast<size_t>(1U - (activeSlot_ & 1U)) *
              SHOT_CURVE_FLASH_SLOT_BYTES,
          SHOT_CURVE_FLASH_SLOT_BYTES, sizeof(ShotCurveHeader), sizeof(store_));
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

  bool containsShotId(uint32_t id) const {
    if (id == 0 || store_.header.count == 0) {
      return false;
    }
    size_t index = store_.header.writeIndex;
    for (size_t n = 0; n < store_.header.count; ++n) {
      if (index == 0) {
        index = SHOT_CURVE_CAPACITY;
      }
      --index;
      if (store_.records[index].shotId == id) {
        return true;
      }
    }
    return false;
  }

  bool copyByShotId(uint32_t id, ShotCurveRecord &output) const {
    if (id == 0 || store_.header.count == 0) return false;
    size_t index = store_.header.writeIndex;
    for (size_t n = 0; n < store_.header.count; ++n) {
      if (index == 0) index = SHOT_CURVE_CAPACITY;
      --index;
      if (store_.records[index].shotId == id) {
        output = store_.records[index];
        return true;
      }
    }
    return false;
  }

  bool removeById(uint32_t id, bool persistNow = true) {
    if (id == 0 || store_.header.count == 0) {
      return false;
    }
    if (persistNow && !lockFlashIo()) {
      return false;
    }
    compactShotCurveStore(store_);
    bool found = false;
    size_t foundIndex = 0;
    for (size_t index = 0; index < store_.header.count; ++index) {
      if (store_.records[index].shotId == id) {
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
                  sizeof(ShotCurveRecord));
    }
    --store_.header.count;
    store_.header.writeIndex =
        static_cast<uint16_t>(store_.header.count % SHOT_CURVE_CAPACITY);
    memset(&store_.records[store_.header.count], 0, sizeof(ShotCurveRecord));
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

  bool clear(bool persistNow = true) {
    resetShotCurveStore(store_);
    if (!persistNow) {
      dirty_ = true;
      return true;
    }
    if (save()) {
      return true;
    }
    load();
    return false;
  }

  size_t count() const { return store_.header.count; }

  void acknowledgePersisted(const ShotCurveLog &image, bool clearDirty) {
    activeSlot_ = image.activeSlot_;
    if (clearDirty) dirty_ = false;
  }

  size_t copyNewestFirst(ShotCurveRecord *output, size_t capacity) const {
    if (output == nullptr || capacity == 0 || store_.header.count == 0) {
      return 0;
    }
    const size_t toCopy =
        store_.header.count < capacity ? store_.header.count : capacity;
    size_t index = store_.header.writeIndex;
    for (size_t copied = 0; copied < toCopy; ++copied) {
      if (index == 0) {
        index = SHOT_CURVE_CAPACITY;
      }
      --index;
      output[copied] = store_.records[index];
    }
    return toCopy;
  }

 private:
#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  static const esp_partition_t *curvePartition() {
    return esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        static_cast<esp_partition_subtype_t>(0x40), "shotcurve");
  }

  static bool readSlot(const esp_partition_t *part, size_t offset,
                       ShotCurveStore &dest) {
    memset(&dest, 0, sizeof(dest));
    if (part == nullptr) {
      return false;
    }
    return flashIoReadChunked(part, offset, &dest, sizeof(dest));
  }
#endif

  ShotCurveStore store_{};
  FlashStoreTransaction persistProgress_{};
  uint8_t activeSlot_ = 0;
  bool dirty_ = false;

#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  static ShotCurveStore hostSlots_[2];
  static bool hostSlotValid_[2];
  static bool hostSaveSucceeds_;
#endif
};

#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
ShotCurveStore ShotCurveLog::hostSlots_[2] = {};
bool ShotCurveLog::hostSlotValid_[2] = {};
bool ShotCurveLog::hostSaveSucceeds_ = true;
#endif

}  // namespace shotstopper
