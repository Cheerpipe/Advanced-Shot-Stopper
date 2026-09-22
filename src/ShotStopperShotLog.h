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

inline uint32_t shotLogRecordId(const ShotLogRecord &record) {
  return record.id;
}

template <>
struct DualSlotFlashLogTraits<ShotLogStore> {
  static constexpr const char *kPartitionName = "shotlog";
  static constexpr size_t kSlotBytes = SHOT_LOG_FLASH_SLOT_BYTES;
  static constexpr size_t kSlotCount = SHOT_LOG_FLASH_SLOT_COUNT;
  static constexpr size_t kHeaderBytes = sizeof(ShotLogHeader);
  static constexpr void (*reset)(ShotLogStore &) = resetShotLogStoreWithBootId;
  using Record = ShotLogRecord;
  static constexpr size_t kCapacity = SHOT_LOG_CAPACITY;
  static constexpr uint32_t (*recordIdOf)(const ShotLogRecord &) =
      shotLogRecordId;
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

  bool updateRating(uint32_t id, uint8_t rating, bool persistNow = true) {
    ShotLogRecord *found = findNewestById(id);
    if (found == nullptr || rating > SHOT_LOG_RATING_MAX) {
      return false;
    }
    found->extractionGuardEnabled =
        shotLogPackRating(found->extractionGuardEnabled, rating);
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

  bool copyRatingById(uint32_t id, uint8_t &rating) const {
    const ShotLogRecord *found = findNewestById(id);
    if (found == nullptr) {
      return false;
    }
    rating = shotLogRating(found->extractionGuardEnabled);
    return true;
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
};

}  // namespace shotstopper
