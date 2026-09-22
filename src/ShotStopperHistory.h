#pragma once

// Activation-history ring store: PSRAM working copy with deferred dual-slot
// flash persistence in the dedicated `history` data partition. The flash
// mechanics live in ShotStopperDualSlotFlashLog.h; this header owns the
// activation-history schema hooks and the record-level API.

#include "ShotStopperDualSlotFlashLog.h"
#include "ShotStopperHistoryTypes.h"
#include "ShotStopperNvsDualSlot.h"

namespace shotstopper {

// Two 16 KiB slots fill the dedicated history data partition.
constexpr size_t HISTORY_FLASH_SLOT_BYTES = 16384;
constexpr size_t HISTORY_FLASH_SLOT_COUNT = 2;

inline uint32_t historyRecordId(const HistoryRecord &record) {
  return record.id;
}

template <>
struct DualSlotFlashLogTraits<HistoryStore> {
  static constexpr const char *kPartitionName = "history";
  static constexpr size_t kSlotBytes = HISTORY_FLASH_SLOT_BYTES;
  static constexpr size_t kSlotCount = HISTORY_FLASH_SLOT_COUNT;
  static constexpr size_t kHeaderBytes = sizeof(HistoryHeader);
  static constexpr void (*reset)(HistoryStore &) = resetHistoryStore;
  using Record = HistoryRecord;
  static constexpr size_t kCapacity = HISTORY_CAPACITY;
  static constexpr uint32_t (*recordIdOf)(const HistoryRecord &) =
      historyRecordId;
};

class HistoryLog
    : public DualSlotFlashLog<HistoryStore, validHistoryStore,
                              compactHistoryStore, finalizeHistoryStore> {
 public:

  bool clear(bool persistNow = true) {
    // Keep the monotonic generation: a regressed generation would make the
    // pre-clear slot look newer on the next load.
    const uint32_t generation = store_.header.generation;
    resetHistoryStore(store_);
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

  uint32_t nextRecordId() const { return store_.header.nextRecordId; }

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

  void acknowledgePersisted(const HistoryLog &image, bool clearDirty) {
    activeSlot_ = image.activeSlot_;
    if (clearDirty) dirty_ = false;
  }
};

}  // namespace shotstopper
