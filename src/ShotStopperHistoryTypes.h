#pragma once

// Independent activation history: one compact record per confirmed machine
// activation (shot, rinse, or short "other"), kept beside the metrics-rich
// stats shot log. Classification is purely time based; no shot metrics.

#include "ShotStopperShotLogTypes.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace shotstopper {

constexpr uint32_t HISTORY_MAGIC = 0x41435448U;  // "ACTH"
constexpr uint16_t HISTORY_SCHEMA_VERSION = 1;
constexpr size_t HISTORY_CAPACITY = 1000;
constexpr size_t HISTORY_PAGE_DEFAULT = 20;
// Bounds one HTTP page copy; the handler pages through the ring by offset.
constexpr size_t HISTORY_PAGE_MAX = 120;

constexpr uint8_t HISTORY_FLAG_WALL_TIME = 0x01;

enum class HistoryType : uint8_t {
  SHOT = 0,
  RINSE = 1,
  OTHER = 2,
};

inline const char *historyTypeName(HistoryType type) {
  switch (type) {
    case HistoryType::SHOT: return "shot";
    case HistoryType::RINSE: return "rinse";
    case HistoryType::OTHER: return "other";
  }
  return "unknown";
}

// A rinse end is always `rinse`. Otherwise an activation strictly longer than
// the cycle's BBW protection window counts as a `shot`; anything shorter is
// `other`. The same strict-greater protection threshold decides stats and
// last-good-shot eligibility.
inline HistoryType historyTypeFromCycle(bool rinseEnd, uint32_t durationMs,
                                        uint32_t protectionMs) {
  if (rinseEnd) {
    return HistoryType::RINSE;
  }
  return durationMs > protectionMs ? HistoryType::SHOT : HistoryType::OTHER;
}

struct HistoryRecord {
  uint32_t id;
  uint32_t endedAtUnixSec;
  uint32_t endedAtLocalSec;
  uint16_t durationDs;
  uint8_t type;   // HistoryType
  uint8_t flags;  // bit 0: hasWallTime
};

static_assert(sizeof(HistoryRecord) == 16,
              "HistoryRecord packing is part of the flash partition schema");

struct HistoryHeader {
  uint32_t magic;
  uint16_t schemaVersion;
  uint16_t recordSize;
  uint32_t generation;
  uint32_t nextRecordId;
  uint16_t count;
  uint16_t writeIndex;
  uint32_t checksum;
};

static_assert(sizeof(HistoryHeader) == 24,
              "HistoryHeader packing is part of the flash partition schema");

struct HistoryStore {
  HistoryHeader header;
  HistoryRecord records[HISTORY_CAPACITY];
};

static_assert(sizeof(HistoryStore) == 16024,
              "HistoryStore must fit the shared flash I/O scratch and one 16 KiB slot");

inline size_t historyClampPageLimit(size_t limit) {
  if (limit < 1U) {
    return 1U;
  }
  if (limit > HISTORY_PAGE_MAX) {
    return HISTORY_PAGE_MAX;
  }
  return limit;
}

inline uint32_t historyChecksum(const HistoryStore &store) {
  uint32_t crc = crc32Update(
      0xFFFFFFFFU, reinterpret_cast<const uint8_t *>(&store.header),
      offsetof(HistoryHeader, checksum));
  if (store.header.count > 0) {
    crc = crc32Update(crc, reinterpret_cast<const uint8_t *>(store.records),
                      static_cast<size_t>(store.header.count) *
                          sizeof(HistoryRecord));
  }
  return ~crc;
}

inline void finalizeHistoryStore(HistoryStore &store) {
  store.header.magic = HISTORY_MAGIC;
  store.header.schemaVersion = HISTORY_SCHEMA_VERSION;
  store.header.recordSize = sizeof(HistoryRecord);
  store.header.checksum = 0;
  store.header.checksum = historyChecksum(store);
}

inline bool validHistoryStore(const HistoryStore &store) {
  if (store.header.magic != HISTORY_MAGIC ||
      store.header.schemaVersion != HISTORY_SCHEMA_VERSION ||
      store.header.recordSize != sizeof(HistoryRecord) ||
      store.header.count > HISTORY_CAPACITY ||
      store.header.writeIndex >= HISTORY_CAPACITY ||
      store.header.checksum != historyChecksum(store)) {
    return false;
  }
  return true;
}

inline void resetHistoryStore(HistoryStore &store) {
  memset(&store, 0, sizeof(store));
  store.header.generation = 1;
  store.header.nextRecordId = 1;
  finalizeHistoryStore(store);
}

inline void compactHistoryStoreInto(HistoryStore &store, HistoryStore &scratch) {
  const uint16_t count = store.header.count;
  if (count == 0) {
    store.header.writeIndex = 0;
    memset(store.records, 0, sizeof(store.records));
    return;
  }

  size_t index = store.header.writeIndex;
  for (uint16_t step = 0; step < count; ++step) {
    if (index == 0) {
      index = HISTORY_CAPACITY;
    }
    --index;
  }
  for (uint16_t i = 0; i < count; ++i) {
    scratch.records[i] = store.records[index];
    index = (index + 1U) % HISTORY_CAPACITY;
  }
  memset(store.records, 0, sizeof(store.records));
  memcpy(store.records, scratch.records,
         static_cast<size_t>(count) * sizeof(HistoryRecord));
  store.header.writeIndex =
      static_cast<uint16_t>(count % HISTORY_CAPACITY);
}

// One bounded page out of the ring. `dir` selects the walk order: Desc pages
// from the newest record, Asc from the oldest, matching the shot-log query
// semantics where Asc reverses the whole listing. Trivial aggregate so it can
// share the network work-buffer union; copyPage fills every field.
struct HistoryPage {
  size_t total;
  size_t start;
  size_t count;
  bool hasMore;
  HistoryRecord records[HISTORY_PAGE_MAX];
};

static_assert(sizeof(HistoryPage) <= 2048,
              "HistoryPage lives in the shared network work buffer union");

}  // namespace shotstopper
