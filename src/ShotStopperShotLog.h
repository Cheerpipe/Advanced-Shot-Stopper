#pragma once

// Stats shot-log ring store: PSRAM working copy with deferred dual-slot flash
// persistence in the dedicated `shotlog` data partition. The flash mechanics
// (generation flip through the inactive slot, chunked internal-SRAM-staged
// transfers while the flash cache is disabled) live in
// ShotStopperDualSlotFlashLog.h; this header owns the shot-log schema hooks
// and the record-level API.

#include "ShotStopperDualSlotFlashLog.h"
#include "ShotStopperShotLogTypes.h"

namespace shotstopper {

// Two 12 KiB slots fill the dedicated shotlog data partition.
constexpr size_t SHOT_LOG_FLASH_SLOT_BYTES = 12288;
constexpr size_t SHOT_LOG_FLASH_SLOT_COUNT = 2;

// Alias matching the DualSlotFlashLogTraits hook signatures (the schema
// validator takes an optional expected schema version).
inline bool validShotLogStoreCurrent(const ShotLogStore &store) {
  return validShotLogStore(store);
}

template <>
struct DualSlotFlashLogTraits<ShotLogStore> {
  static constexpr const char *kPartitionName = "shotlog";
  static constexpr size_t kSlotBytes = SHOT_LOG_FLASH_SLOT_BYTES;
  static constexpr size_t kSlotCount = SHOT_LOG_FLASH_SLOT_COUNT;
  static constexpr size_t kHeaderBytes = sizeof(ShotLogHeader);
  static constexpr void (*reset)(ShotLogStore &) = resetShotLogStoreWithBootId;
};

class ShotLog
    : public DualSlotFlashLog<ShotLogStore, validShotLogStoreCurrent,
                              compactShotLogStore, finalizeShotLogStore> {
 public:
  void onBoot() {
    if (store_.header.bootId == 0) {
      store_.header.bootId = 1;
    } else if (store_.header.bootId < UINT32_MAX) {
      ++store_.header.bootId;
    }
    dirty_ = true;
  }

  uint32_t bootId() const { return store_.header.bootId; }

  uint32_t nextRecordId() const { return store_.header.nextRecordId; }

  bool append(const ShotLogRecord &record, bool persistNow = true) {
    const uint32_t lockTimeoutsBefore = flashIoLockTimeouts();
    const uint16_t previousWriteIndex = store_.header.writeIndex;
    const uint16_t previousCount = store_.header.count;
    const uint32_t previousNextRecordId = store_.header.nextRecordId;
    const ShotLogRecord overwritten = store_.records[previousWriteIndex];

    ShotLogRecord stored = record;
    stored.id = store_.header.nextRecordId;
    if (store_.header.nextRecordId < UINT32_MAX) {
      ++store_.header.nextRecordId;
    }
    store_.records[store_.header.writeIndex] = stored;
    store_.header.writeIndex =
        static_cast<uint16_t>((store_.header.writeIndex + 1U) %
                              SHOT_LOG_CAPACITY);
    if (store_.header.count < SHOT_LOG_CAPACITY) {
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
      // save() may have compacted the ring before the flash write failed, so
      // the pre-append snapshot no longer describes the live layout —
      // restoring it would clobber a compacted record. Reload the last-good
      // store instead (same pattern as updateRating/removeById).
      load();
      return false;
    }
    store_.records[previousWriteIndex] = overwritten;
    store_.header.writeIndex = previousWriteIndex;
    store_.header.count = previousCount;
    store_.header.nextRecordId = previousNextRecordId;
    return false;
  }

  bool updateRating(uint32_t id, uint8_t rating, bool persistNow = true) {
    if (id == 0 || rating > SHOT_LOG_RATING_MAX || store_.header.count == 0) {
      return false;
    }
    size_t index = store_.header.writeIndex;
    bool found = false;
    for (size_t n = 0; n < store_.header.count; ++n) {
      if (index == 0) {
        index = SHOT_LOG_CAPACITY;
      }
      --index;
      if (store_.records[index].id == id) {
        store_.records[index].extractionGuardEnabled = shotLogPackRating(
            store_.records[index].extractionGuardEnabled, rating);
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
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

  bool containsId(uint32_t id) const {
    if (id == 0 || store_.header.count == 0) {
      return false;
    }
    size_t index = store_.header.writeIndex;
    for (size_t n = 0; n < store_.header.count; ++n) {
      if (index == 0) {
        index = SHOT_LOG_CAPACITY;
      }
      --index;
      if (store_.records[index].id == id) {
        return true;
      }
    }
    return false;
  }

  bool copyRatingById(uint32_t id, uint8_t &rating) const {
    if (id == 0 || store_.header.count == 0) return false;
    size_t index = store_.header.writeIndex;
    for (size_t n = 0; n < store_.header.count; ++n) {
      if (index == 0) index = SHOT_LOG_CAPACITY;
      --index;
      if (store_.records[index].id == id) {
        rating = shotLogRating(store_.records[index].extractionGuardEnabled);
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
    // Compact to a linear prefix so deletion is a memmove, avoiding two
    // SHOT_LOG_CAPACITY arrays on the 8 KB loopTask stack.
    compactShotLogStore(store_);
    bool found = false;
    size_t foundIndex = 0;
    for (size_t index = 0; index < store_.header.count; ++index) {
      if (store_.records[index].id == id) {
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
                  sizeof(ShotLogRecord));
    }
    --store_.header.count;
    store_.header.writeIndex =
        static_cast<uint16_t>(store_.header.count % SHOT_LOG_CAPACITY);
    memset(&store_.records[store_.header.count], 0, sizeof(ShotLogRecord));
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
    const uint32_t bootId = store_.header.bootId;
    // Keep the monotonic generation: a regressed generation would make the
    // pre-clear slot look newer on the next load.
    const uint32_t generation = store_.header.generation;
    resetShotLogStoreWithBootId(store_);
    store_.header.bootId = bootId;
    store_.header.generation = generation;
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

  void acknowledgePersisted(const ShotLog &image, bool clearDirty) {
    activeSlot_ = image.activeSlot_;
    if (clearDirty) dirty_ = false;
  }

  size_t copyNewestFirst(ShotLogRecord *output, size_t capacity) const {
    if (output == nullptr || capacity == 0 || store_.header.count == 0) {
      return 0;
    }
    const size_t toCopy =
        store_.header.count < capacity ? store_.header.count : capacity;
    size_t index = store_.header.writeIndex;
    for (size_t copied = 0; copied < toCopy; ++copied) {
      if (index == 0) {
        index = SHOT_LOG_CAPACITY;
      }
      --index;
      output[copied] = store_.records[index];
    }
    return toCopy;
  }
};

}  // namespace shotstopper
