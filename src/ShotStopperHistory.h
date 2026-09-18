#pragma once

// Activation-history ring store: PSRAM working copy with deferred dual-slot
// flash persistence in the dedicated `history` data partition. Mirrors the
// ShotCurveLog flash contract (generation flip through the inactive slot,
// chunked internal-SRAM-staged transfers while the flash cache is disabled).

#include "ShotStopperFlashIoScratch.h"
#include "ShotStopperHistoryTypes.h"
#include "ShotStopperNvsDualSlot.h"

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <esp_partition.h>
#endif

namespace shotstopper {

// Two 16 KiB slots fill the dedicated history data partition.
constexpr size_t HISTORY_FLASH_SLOT_BYTES = 16384;
constexpr size_t HISTORY_FLASH_SLOT_COUNT = 2;

class HistoryLog {
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
      if (!hostSlotValid_[slot] || !validHistoryStore(hostSlots_[slot])) {
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
      resetHistoryStore(store_);
      activeSlot_ = 0;
    }
    dirty_ = false;
    return true;
#else
    if (!lockFlashIo()) {
      resetHistoryStore(store_);
      dirty_ = false;
      return false;
    }
    const esp_partition_t *part = historyPartition();
    if (part == nullptr ||
        part->size < HISTORY_FLASH_SLOT_COUNT * HISTORY_FLASH_SLOT_BYTES) {
      resetHistoryStore(store_);
      dirty_ = false;
      unlockFlashIo();
      return false;
    }

    const bool aOk =
        readSlot(part, 0, store_) && validHistoryStore(store_);
    const uint32_t gen0 = aOk ? store_.header.generation : 0;
    const bool bOk =
        readSlot(part, HISTORY_FLASH_SLOT_BYTES, store_) &&
        validHistoryStore(store_);
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
      resetHistoryStore(store_);
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
    compactHistoryStore(store_);
    if (store_.header.generation == 0) {
      store_.header.generation = 1;
    } else if (store_.header.generation < UINT32_MAX) {
      ++store_.header.generation;
    }
    finalizeHistoryStore(store_);
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
    const esp_partition_t *part = historyPartition();
    if (part == nullptr ||
        part->size < HISTORY_FLASH_SLOT_COUNT * HISTORY_FLASH_SLOT_BYTES) {
      unlockFlashIo();
      return false;
    }
    yieldFlashIo();
    feedFlashIoWatchdog();
    const size_t targetOffset =
        static_cast<size_t>(targetSlot) * HISTORY_FLASH_SLOT_BYTES;
    if (esp_partition_erase_range(part, targetOffset,
                                  HISTORY_FLASH_SLOT_BYTES) != ESP_OK) {
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

  bool append(const HistoryRecord &record, bool persistNow = true) {
    const uint32_t lockTimeoutsBefore = flashIoLockTimeouts();
    const uint16_t previousWriteIndex = store_.header.writeIndex;
    const uint16_t previousCount = store_.header.count;
    const uint32_t previousNextRecordId = store_.header.nextRecordId;
    const HistoryRecord overwritten = store_.records[previousWriteIndex];

    HistoryRecord stored = record;
    stored.id = store_.header.nextRecordId;
    if (store_.header.nextRecordId < UINT32_MAX) {
      ++store_.header.nextRecordId;
    }
    store_.records[store_.header.writeIndex] = stored;
    store_.header.writeIndex = static_cast<uint16_t>(
        (store_.header.writeIndex + 1U) % HISTORY_CAPACITY);
    if (store_.header.count < HISTORY_CAPACITY) {
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

  size_t count() const { return store_.header.count; }

  uint32_t nextRecordId() const { return store_.header.nextRecordId; }

  bool containsId(uint32_t id) const {
    if (id == 0 || store_.header.count == 0) {
      return false;
    }
    size_t index = store_.header.writeIndex;
    for (size_t n = 0; n < store_.header.count; ++n) {
      if (index == 0) {
        index = HISTORY_CAPACITY;
      }
      --index;
      if (store_.records[index].id == id) {
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
    compactHistoryStore(store_);
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
                  sizeof(HistoryRecord));
    }
    --store_.header.count;
    store_.header.writeIndex =
        static_cast<uint16_t>(store_.header.count % HISTORY_CAPACITY);
    memset(&store_.records[store_.header.count], 0, sizeof(HistoryRecord));
    const bool saved = save();
    unlockFlashIo();
    if (saved) {
      return true;
    }
    load();
    return false;
  }

  bool clear() {
    // Keep the monotonic generation: a regressed generation would make the
    // pre-clear slot look newer on the next load.
    const uint32_t generation = store_.header.generation;
    resetHistoryStore(store_);
    store_.header.generation = generation;
    if (save()) {
      return true;
    }
    load();
    return false;
  }

  // Fills one bounded page (see HistoryPage). offset counts from the newest
  // record for Desc and from the oldest for Asc. Caller holds the store mutex.
  void copyPage(HistoryPage &page, size_t offset, size_t limit,
                ShotLogSortDir dir) const {
    const size_t total = store_.header.count;
    limit = historyClampPageLimit(limit);
    if (offset > total) {
      offset = total;
    }
    page.total = total;
    page.start = offset;
    page.hasMore = false;
    page.count = 0;
    const size_t remaining = total - offset;
    const size_t pageCount = remaining < limit ? remaining : limit;
    if (pageCount == 0) {
      return;
    }
    page.hasMore = (offset + pageCount) < total;
    if (dir == ShotLogSortDir::Desc) {
      // Walk backwards from writeIndex, skipping the `offset` newest records.
      size_t index = store_.header.writeIndex;
      for (size_t step = 0; step < offset; ++step) {
        if (index == 0) {
          index = HISTORY_CAPACITY;
        }
        --index;
      }
      for (size_t copied = 0; copied < pageCount; ++copied) {
        if (index == 0) {
          index = HISTORY_CAPACITY;
        }
        --index;
        page.records[copied] = store_.records[index];
      }
    } else {
      // Walk forward from the oldest retained record.
      size_t index =
          (store_.header.writeIndex + HISTORY_CAPACITY - total) %
          HISTORY_CAPACITY;
      index = (index + offset) % HISTORY_CAPACITY;
      for (size_t copied = 0; copied < pageCount; ++copied) {
        page.records[copied] = store_.records[index];
        index = (index + 1U) % HISTORY_CAPACITY;
      }
    }
    page.count = pageCount;
  }

 private:
#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  static const esp_partition_t *historyPartition() {
    return esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        static_cast<esp_partition_subtype_t>(0x40), "history");
  }

  static bool readSlot(const esp_partition_t *part, size_t offset,
                       HistoryStore &dest) {
    memset(&dest, 0, sizeof(dest));
    if (part == nullptr) {
      return false;
    }
    return flashIoReadChunked(part, offset, &dest, sizeof(dest));
  }
#endif

  HistoryStore store_{};
  uint8_t activeSlot_ = 0;
  bool dirty_ = false;

#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  static HistoryStore hostSlots_[2];
  static bool hostSlotValid_[2];
  static bool hostSaveSucceeds_;
#endif
};

#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
HistoryStore HistoryLog::hostSlots_[2] = {};
bool HistoryLog::hostSlotValid_[2] = {};
bool HistoryLog::hostSaveSucceeds_ = true;
#endif

}  // namespace shotstopper
