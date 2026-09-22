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

template <>
struct DualSlotFlashLogTraits<ShotCurveStore> {
  static constexpr const char *kPartitionName = "shotcurve";
  static constexpr size_t kSlotBytes = SHOT_CURVE_FLASH_SLOT_BYTES;
  static constexpr size_t kSlotCount = SHOT_CURVE_FLASH_SLOT_COUNT;
  static constexpr size_t kHeaderBytes = sizeof(ShotCurveHeader);
  static constexpr void (*reset)(ShotCurveStore &) = resetShotCurveStore;
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
};

}  // namespace shotstopper
