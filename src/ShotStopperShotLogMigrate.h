#pragma once

// V1/V2 -> V3 retains the same record layout and initializes newly used bits.
// Older firmware rejects V3; choosing Legacy in current firmware is supported.
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

// `bytes` may alias `out` for the current schema. Future migrations that
// memset the destination must reject aliasing for those legacy sizes.
inline ShotLogDecodeStatus decodeShotLogBlob(const void *bytes, size_t length,
                                             ShotLogStore &out) {
  if (bytes == nullptr || length < sizeof(ShotLogHeader) || length > sizeof(ShotLogStore)) {
    return ShotLogDecodeStatus::INVALID;
  }

  const auto *asCurrent = reinterpret_cast<const ShotLogStore *>(bytes);
  const uint16_t version = asCurrent->header.schemaVersion;
  const bool legacy = version == 1 || version == 2;
  if (shotLogBlobLengthMatches(*asCurrent, length) &&
      validShotLogStore(*asCurrent, legacy ? version : SHOT_LOG_SCHEMA_VERSION)) {
    if (&out != asCurrent) {
      memset(&out, 0, sizeof(out));
      memcpy(&out, bytes, length);
    }
    if (legacy) {
      for (ShotLogRecord &record : out.records) {
        if (version == 1) {
          record.extractionGuardEnabled = (record.extractionGuardEnabled & 0x1f) | 0x20;
          record.extractionExtended &= 3;
        }
        shotLogSetPresetId(record, 0);  // Never infer a past preset from current settings.
      }
      finalizeShotLogStore(out);
      return ShotLogDecodeStatus::MIGRATED;
    }
    return ShotLogDecodeStatus::CURRENT;
  }
  return ShotLogDecodeStatus::INVALID;
}

}  // namespace shotstopper
