#pragma once

// V1–V4 -> V5 preserves prior metrics and adds an empty preset-name snapshot.
// V1–V3 also convert legacy alpha codes to hundredths.
//
// When bumping SHOT_LOG_SCHEMA_VERSION:
// 1. Keep the previous record/store layout as ShotLogRecordV<N> / StoreV<N>.
// 2. Add migrateShotLogStoreV<N>(...) and a validShotLogStoreV<N>(...) check.
// 3. Extend decodeShotLogBlob() to accept that length/version and return
//    ShotLogDecodeStatus::MIGRATED after rewriting into the current store.
// 4. Cover the path in persistence_host_test.cpp.

#include "ShotStopperShotLogTypes.h"

namespace shotstopper {

enum class ShotLogDecodeStatus : uint8_t {
  CURRENT = 0,
  MIGRATED = 1,
  INVALID = 2,
};

struct ShotLogRecordV4 {
  uint32_t id;
  uint32_t bootId;
  uint32_t endedAtMs;
  uint32_t endedAtUnixSec;
  uint32_t endedAtLocalSec;
  int16_t timezoneOffsetMinutesAtCommit;
  uint16_t durationDs;
  uint8_t goalWeightG;
  uint8_t hasWallTime;
  int16_t actualWeightCg;
  int16_t errorCg;
  int16_t offsetUsedCg;
  uint16_t firstDropDs;
  uint16_t avgFlowCgS;
  uint8_t shotType;
  uint8_t cutType;
  uint8_t extractionGuardEnabled;
  uint8_t extractionExtended;
  uint8_t stopDetail;
  uint8_t actualWeightSource;
  int16_t maxRecoveryWeightCg;
  uint16_t minBbwBrewTimeDs;
  uint16_t targetReachedEarlyDs;
};

struct ShotLogStoreV4 {
  ShotLogHeader header;
  ShotLogRecordV4 records[SHOT_LOG_CAPACITY];
};

static_assert(sizeof(ShotLogRecordV4) == 48 &&
                  offsetof(ShotLogRecord, presetName) == sizeof(ShotLogRecordV4),
              "V5 must preserve the complete V4 record prefix");

inline uint32_t shotLogChecksumV4(const ShotLogStoreV4 &store) {
  uint32_t crc = crc32Update(
      0xFFFFFFFFU, reinterpret_cast<const uint8_t *>(&store.header),
      offsetof(ShotLogHeader, checksum));
  if (store.header.count > 0) {
    crc = crc32Update(crc, reinterpret_cast<const uint8_t *>(store.records),
                      static_cast<size_t>(store.header.count) *
                          sizeof(ShotLogRecordV4));
  }
  return ~crc;
}

inline bool validShotLogStoreV4(const ShotLogStoreV4 &store, size_t length) {
  const size_t packed = sizeof(ShotLogHeader) +
      static_cast<size_t>(store.header.count) * sizeof(ShotLogRecordV4);
  return store.header.magic == SHOT_LOG_MAGIC &&
         store.header.schemaVersion >= 1 && store.header.schemaVersion <= 4 &&
         store.header.recordSize == sizeof(ShotLogRecordV4) &&
         store.header.count <= SHOT_LOG_CAPACITY &&
         store.header.writeIndex < SHOT_LOG_CAPACITY &&
         (length == sizeof(ShotLogStoreV4) || length == packed) &&
         store.header.checksum == shotLogChecksumV4(store);
}

// `bytes` may alias `out`; legacy records expand backwards before unused
// destination records are cleared so compact inputs remain safe.
inline ShotLogDecodeStatus decodeShotLogBlob(const void *bytes, size_t length,
                                             ShotLogStore &out) {
  if (bytes == nullptr || length < sizeof(ShotLogHeader) || length > sizeof(ShotLogStore)) {
    return ShotLogDecodeStatus::INVALID;
  }

  const auto *asCurrent = reinterpret_cast<const ShotLogStore *>(bytes);
  const uint16_t version = asCurrent->header.schemaVersion;
  if (version == SHOT_LOG_SCHEMA_VERSION &&
      shotLogBlobLengthMatches(*asCurrent, length) &&
      validShotLogStore(*asCurrent)) {
    if (&out != asCurrent) {
      memset(&out, 0, sizeof(out));
      memcpy(&out, bytes, length);
    }
    return ShotLogDecodeStatus::CURRENT;
  }
  if (version >= 1 && version <= 4 && length <= sizeof(ShotLogStoreV4)) {
    const auto *legacy = reinterpret_cast<const ShotLogStoreV4 *>(bytes);
    if (!validShotLogStoreV4(*legacy, length)) {
      return ShotLogDecodeStatus::INVALID;
    }
    const ShotLogHeader header = legacy->header;
    for (size_t i = header.count; i > 0; --i) {
      const ShotLogRecordV4 record = legacy->records[i - 1U];
      memset(&out.records[i - 1U], 0, sizeof(ShotLogRecord));
      memcpy(&out.records[i - 1U], &record, sizeof(record));
    }
    memset(&out.records[header.count], 0,
           (SHOT_LOG_CAPACITY - header.count) * sizeof(ShotLogRecord));
    out.header = header;
    if (version <= 3) {
      for (ShotLogRecord &record : out.records) {
        if (version == 1) {
          record.extractionGuardEnabled =
              (record.extractionGuardEnabled & 0x1f) | 0x20;
          record.extractionExtended &= 3;
        }
        const uint8_t code = (record.extractionExtended >> 2) & 7;
        const uint8_t alpha = code >= 1 && code <= 4
                                  ? BBW_ALPHA_CANDIDATES[code - 1]
                                  : 0;
        record.extractionExtended =
            (record.extractionExtended & 0x63) | ((alpha & 7) << 2);
        record.cutType = (record.cutType & 15) | ((alpha >> 3) << 4);
        if (version < 3) shotLogSetPresetId(record, 0);
      }
    }
    finalizeShotLogStore(out);
    return ShotLogDecodeStatus::MIGRATED;
  }
  return ShotLogDecodeStatus::INVALID;
}

}  // namespace shotstopper
