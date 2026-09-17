#pragma once

// Stats shot-log ring store: PSRAM working copy with deferred dual-slot flash
// persistence in the dedicated `shotlog` data partition. Mirrors the
// ShotCurveLog flash contract (generation flip through the inactive slot,
// internal-SRAM scratch copies while the flash cache is disabled).

#include "ShotStopperFlashIoScratch.h"
#include "ShotStopperNvsDualSlot.h"
#include "ShotStopperShotLogTypes.h"

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <esp_partition.h>
#endif

namespace shotstopper {

// Two 12 KiB slots fill the dedicated shotlog data partition.
constexpr size_t SHOT_LOG_FLASH_SLOT_BYTES = 12288;
constexpr size_t SHOT_LOG_FLASH_SLOT_COUNT = 2;

inline ShotLogStore &shotLogScratchStore() {
  static_assert(sizeof(ShotLogStore) <= FLASH_IO_SCRATCH_BYTES,
                "ShotLogStore exceeds shared flash I/O scratch");
  return *reinterpret_cast<ShotLogStore *>(flashIoScratchBytes());
}

// Pack the ring into records[0..count) (oldest first) so the flash slot
// always holds the used prefix and deletion is a memmove.
// Caller must already hold lockFlashIo() (recursive).
inline void compactShotLogStore(ShotLogStore &store) {
  const uint16_t count = store.header.count;
  if (count == 0) {
    store.header.writeIndex = 0;
    memset(store.records, 0, sizeof(store.records));
    return;
  }

  ShotLogStore &scratch = shotLogScratchStore();
  size_t index = store.header.writeIndex;
  for (uint16_t step = 0; step < count; ++step) {
    if (index == 0) {
      index = SHOT_LOG_CAPACITY;
    }
    --index;
  }
  for (uint16_t i = 0; i < count; ++i) {
    scratch.records[i] = store.records[index];
    index = (index + 1U) % SHOT_LOG_CAPACITY;
  }
  memset(store.records, 0, sizeof(store.records));
  memcpy(store.records, scratch.records,
         static_cast<size_t>(count) * sizeof(ShotLogRecord));
  store.header.writeIndex =
      static_cast<uint16_t>(count % SHOT_LOG_CAPACITY);
}

class ShotLog {
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
      if (!hostSlotValid_[slot] || !validShotLogStore(hostSlots_[slot])) {
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
      resetShotLogStore(store_, 1);
      activeSlot_ = 0;
    }
    dirty_ = false;
    return true;
#else
    if (!lockFlashIo()) {
      resetShotLogStore(store_, 1);
      dirty_ = false;
      return false;
    }
    const esp_partition_t *part = shotLogPartition();
    if (part == nullptr ||
        part->size < SHOT_LOG_FLASH_SLOT_COUNT * SHOT_LOG_FLASH_SLOT_BYTES) {
      resetShotLogStore(store_, 1);
      dirty_ = false;
      unlockFlashIo();
      return false;
    }

    const bool aOk =
        readSlot(part, 0, store_) && validShotLogStore(store_);
    const uint32_t gen0 = aOk ? store_.header.generation : 0;
    ShotLogStore &slotB = shotLogScratchStore();
    const bool bOk =
        readSlot(part, SHOT_LOG_FLASH_SLOT_BYTES, slotB) &&
        validShotLogStore(slotB);
    const DualSlotChoice choice =
        chooseNewerRevision(aOk, gen0, bOk, slotB.header.generation);
    if (choice == DualSlotChoice::SECOND) {
      memcpy(&store_, &slotB, sizeof(store_));
      activeSlot_ = 1;
    } else if (choice == DualSlotChoice::FIRST) {
      // store_ still holds slot A; slot B only occupied scratch.
      if (!aOk) {
        (void)readSlot(part, 0, store_);
      }
      activeSlot_ = 0;
    } else {
      resetShotLogStore(store_, 1);
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
    compactShotLogStore(store_);
    if (store_.header.generation == 0) {
      store_.header.generation = 1;
    } else if (store_.header.generation < UINT32_MAX) {
      ++store_.header.generation;
    }
    finalizeShotLogStore(store_);
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
    const esp_partition_t *part = shotLogPartition();
    if (part == nullptr ||
        part->size < SHOT_LOG_FLASH_SLOT_COUNT * SHOT_LOG_FLASH_SLOT_BYTES) {
      unlockFlashIo();
      return false;
    }
    yieldFlashIo();
    feedFlashIoWatchdog();
    const size_t targetOffset =
        static_cast<size_t>(targetSlot) * SHOT_LOG_FLASH_SLOT_BYTES;
    // Source is internal SRAM scratch: the live store_ sits in PSRAM BSS,
    // unreachable while the flash cache is disabled during the slot write.
    void *source = copyToFlashIoScratch(&store_, sizeof(store_));
    if (source == nullptr) {
      unlockFlashIo();
      return false;
    }
    if (esp_partition_erase_range(part, targetOffset,
                                  SHOT_LOG_FLASH_SLOT_BYTES) != ESP_OK) {
      unlockFlashIo();
      return false;
    }
    yieldFlashIo();
    feedFlashIoWatchdog();
    if (esp_partition_write(part, targetOffset, source, sizeof(store_)) !=
        ESP_OK) {
      unlockFlashIo();
      return false;
    }
    activeSlot_ = targetSlot;
    dirty_ = false;
    unlockFlashIo();
    return true;
#endif
  }

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

  bool flush(uint32_t lockTimeoutMs = FLASH_IO_CONTROL_LOCK_TIMEOUT_MS) {
    if (!dirty_) {
      return true;
    }
    return save(lockTimeoutMs);
  }

  bool dirty() const { return dirty_; }

  bool updateRating(uint32_t id, uint8_t rating) {
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

  bool removeById(uint32_t id) {
    if (id == 0 || store_.header.count == 0) {
      return false;
    }
    if (!lockFlashIo()) {
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
      unlockFlashIo();
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
    const bool saved = save();
    unlockFlashIo();
    if (saved) {
      return true;
    }
    load();
    return false;
  }

  bool clear() {
    const uint32_t bootId = store_.header.bootId;
    // Keep the monotonic generation: a regressed generation would make the
    // pre-clear slot look newer on the next load.
    const uint32_t generation = store_.header.generation;
    resetShotLogStore(store_, bootId);
    store_.header.generation = generation;
    if (save()) {
      return true;
    }
    load();
    return false;
  }

  size_t count() const { return store_.header.count; }

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

 private:
#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  static const esp_partition_t *shotLogPartition() {
    return esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        static_cast<esp_partition_subtype_t>(0x40), "shotlog");
  }

  static bool readSlot(const esp_partition_t *part, size_t offset,
                       ShotLogStore &dest) {
    memset(&dest, 0, sizeof(dest));
    if (part == nullptr) {
      return false;
    }
    void *scratch = flashIoScratchBytes();
    if (scratch == nullptr) {
      return false;
    }
    if (esp_partition_read(part, offset, scratch, sizeof(ShotLogStore)) !=
        ESP_OK) {
      return false;
    }
    memcpy(&dest, scratch, sizeof(ShotLogStore));
    return true;
  }
#endif

  ShotLogStore store_{};
  uint8_t activeSlot_ = 0;
  bool dirty_ = false;

#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  static ShotLogStore hostSlots_[2];
  static bool hostSlotValid_[2];
  static bool hostSaveSucceeds_;
#endif
};

#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
ShotLogStore ShotLog::hostSlots_[2] = {};
bool ShotLog::hostSlotValid_[2] = {};
bool ShotLog::hostSaveSucceeds_ = true;
#endif

}  // namespace shotstopper
