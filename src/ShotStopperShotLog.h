#pragma once

// Stats shot-log ring store: PSRAM working copy with deferred dual-slot flash
// persistence in the dedicated `shotlog` data partition. The flash mechanics
// (generation flip through the inactive slot, chunked internal-SRAM-staged
// transfers across flash paths that may disable cache) live in
// ShotStopperDualSlotFlashLog.h; this header owns the shot-log schema hooks
// and the record-level API.

#include "ShotStopperDualSlotFlashLog.h"
#include "ShotStopperShotLogTypes.h"

namespace shotstopper {

// Two 12 KiB slots fill the dedicated shotlog data partition.
constexpr size_t SHOT_LOG_FLASH_SLOT_BYTES = 12288;
constexpr size_t SHOT_LOG_FLASH_SLOT_COUNT = 2;

// Only the fixed v2 layout is accepted; any older slot is discarded.
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
    store_.header.schemaVersion = SHOT_LOG_SCHEMA_VERSION;
    dirty_ = true;
  }

  uint32_t bootId() const { return store_.header.bootId; }

  uint32_t nextRecordId() const { return store_.header.nextRecordId; }

  // Change epoch for stream gating, mirroring HistoryLog::epoch(): one CRC
  // over the header words every mutation moves (rating bumps generation —
  // see updateRating; a full-ring append only advances nextRecordId). Curve
  // stores mutate only alongside these words. Caller holds the store mutex.
  uint32_t epoch() const {
    uint32_t crc = crc32Update(0xFFFFFFFFU,
        reinterpret_cast<const uint8_t *>(&store_.header.generation),
        sizeof(store_.header.generation));
    crc = crc32Update(crc,
        reinterpret_cast<const uint8_t *>(&store_.header.nextRecordId),
        sizeof(store_.header.nextRecordId));
    return crc32Update(crc,
        reinterpret_cast<const uint8_t *>(&store_.header.count),
        sizeof(store_.header.count));
  }

  ShotStatsView statsView() const {
    ShotLogRecord eligible[SHOT_LOG_STATS_WINDOW];
    size_t found = 0;
    uint32_t bbwCount = 0;
    uint32_t bbwErrorTenthsSum = 0;
    size_t index = store_.header.writeIndex;
    for (size_t n = 0; n < store_.header.count &&
                       (found < SHOT_LOG_STATS_WINDOW ||
                        bbwCount < SHOT_LOG_STATS_WINDOW); ++n) {
      if (index == 0) index = SHOT_LOG_CAPACITY;
      const ShotLogRecord &record = store_.records[--index];
      if (!shotLogRecordEligible(record)) continue;
      if (found < SHOT_LOG_STATS_WINDOW) eligible[found++] = record;
      if (bbwCount < SHOT_LOG_STATS_WINDOW &&
          shotLogBbwErrorEligible(record)) {
        ++bbwCount;
        bbwErrorTenthsSum += shotLogBbwErrorTenths(record);
      }
    }
    ShotStatsView stats = shotLogStatsView(eligible, found);
    stats.bbwCount = bbwCount;
    stats.errorPctTenthsSum = bbwErrorTenthsSum;
    return stats;
  }

  bool copyNewestEligible(ShotLogRecord &output) const {
    size_t index = store_.header.writeIndex;
    for (size_t n = 0; n < store_.header.count; ++n) {
      if (index == 0) index = SHOT_LOG_CAPACITY;
      const ShotLogRecord &record = store_.records[--index];
      if (shotLogRecordEligible(record)) {
        output = record;
        return true;
      }
    }
    return false;
  }

  bool updateRating(uint32_t id, uint8_t rating, bool persistNow = true) {
    ShotLogRecord *found = findNewestById(id);
    if (found == nullptr || rating > SHOT_LOG_RATING_MAX) {
      return false;
    }
    found->extractionGuardEnabled =
        shotLogPackRating(found->extractionGuardEnabled, rating);
    // The record bytes changed without moving count or nextRecordId, so bump
    // generation: the modified slot content must win dual-slot selection and
    // stream epochs hash these header words.
    ++store_.header.generation;
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
};

}  // namespace shotstopper
