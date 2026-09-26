#pragma once

// Flash shot-curve sidecar ring store: PSRAM working copy with deferred
// dual-slot flash persistence in the dedicated `shotcurve` data partition.
// The flash mechanics live in ShotStopperDualSlotFlashLog.h; this header owns
// the shot-curve schema hooks and the record-level API.

#include "ShotStopperDualSlotFlashLog.h"
#include "ShotStopperShotCurveTypes.h"

namespace shotstopper {

// One dedicated partition with two 28 KiB slots.
constexpr size_t SHOT_CURVE_FLASH_SLOT_BYTES = 28672;
constexpr size_t SHOT_CURVE_FLASH_SLOT_COUNT = 2;

inline uint32_t shotCurveRecordId(const ShotCurveRecord &record) {
  return record.shotId;
}

template <>
struct DualSlotFlashLogTraits<ShotCurveStore> {
  static constexpr const char *kPartitionName = "shotcurve";
  static constexpr size_t kSlotBytes = SHOT_CURVE_FLASH_SLOT_BYTES;
  static constexpr size_t kSlotCount = SHOT_CURVE_FLASH_SLOT_COUNT;
  static constexpr size_t kHeaderBytes = sizeof(ShotCurveHeader);
  static constexpr void (*reset)(ShotCurveStore &) = resetShotCurveStore;
  using Record = ShotCurveRecord;
  static constexpr size_t kCapacity = SHOT_CURVE_CAPACITY;
  static constexpr uint32_t (*recordIdOf)(const ShotCurveRecord &) =
      shotCurveRecordId;
};

class ShotCurveLog
    : public DualSlotFlashLog<ShotCurveStore, validShotCurveStore,
                              compactShotCurveStore, finalizeShotCurveStore> {
 public:
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

  bool containsShotId(uint32_t id) const {
    return findNewestById(id) != nullptr;
  }

  bool copyByShotId(uint32_t id, ShotCurveRecord &output) const {
    const ShotCurveRecord *found = findNewestById(id);
    if (found == nullptr) {
      return false;
    }
    output = *found;
    return true;
  }

  bool clear(bool persistNow = true) {
    const uint32_t generation = store_.header.generation;
    resetShotCurveStore(store_);
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
};

}  // namespace shotstopper
