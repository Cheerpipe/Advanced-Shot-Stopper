#pragma once

// Accepted extraction/drip observations with reception times and event annotations.
// Flash sidecar, separate from the scalar ShotLogRecord.

#include "ShotStopperShotLogTypes.h"

#include <cmath>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace shotstopper {

constexpr uint32_t SHOT_CURVE_MAGIC = 0x53435256U;  // "SCRV"
// Schema 1: the smooth-cut epoch; the "extended" event marks the cut line
// leaving the target. Schema 2 adds the per-sample held bitmap; schema 3
// delta-encodes the flash payload. Schema-1/2 blocks (absolute points,
// nothing held pre-schema-2) still load, so an update keeps the shot history.
constexpr uint16_t SHOT_CURVE_SCHEMA_VERSION = 3;
constexpr uint32_t SHOT_CURVE_MAX_TIME_MS = 60000;
// Capacity assumption only, never a sampling grid or rate limiter.
constexpr size_t SHOT_CURVE_MAX_POINTS = SHOT_CURVE_MAX_TIME_MS / 50U + 1U;
constexpr size_t SHOT_CURVE_BREAK_BYTES = (SHOT_CURVE_MAX_POINTS + 7U) / 8U;
constexpr size_t SHOT_CURVE_CAPACITY = SHOT_LOG_CAPACITY;
// Worst case: 1201 × (7 weight + 6 time + 5 break-index + 5 held-index
// characters), plus events.
constexpr size_t SHOT_CURVE_JSON_CAPACITY = 28032;

struct ShotCurveEvent {
  uint16_t atMs;
  int16_t weightCg;
};

static_assert(sizeof(ShotCurveEvent) == 4,
              "ShotCurveEvent packing is part of the flash sidecar schema");

inline ShotCurveEvent missingShotCurveEvent() {
  ShotCurveEvent event = {};
  event.atMs = SHOT_LOG_METRIC_MISSING;
  event.weightCg = SHOT_LOG_WEIGHT_MISSING;
  return event;
}

inline bool shotCurveEventPresent(const ShotCurveEvent &event) {
  return event.atMs != SHOT_LOG_METRIC_MISSING &&
         !shotLogWeightIsMissing(event.weightCg);
}

struct ShotCurveRecord {
  uint32_t shotId;
  uint16_t count;
  bool truncated;
  uint8_t reserved;
  uint16_t atmClearedMs;
  ShotCurveEvent firstDrop;
  ShotCurveEvent extended;
  ShotCurveEvent atm;
  ShotCurveEvent ended;
  int16_t weightCg[SHOT_CURVE_MAX_POINTS];
  uint16_t atMs[SHOT_CURVE_MAX_POINTS];
  uint8_t breakBefore[SHOT_CURVE_BREAK_BYTES];
  // Samples the touch classifier held: recorded, never control weight.
  uint8_t held[SHOT_CURVE_BREAK_BYTES];
};

// Network-owned PSRAM copy, captured with one store lock; never a stack
// local. Records only: the streaming Stats page sorts and aggregates from
// these, and each row's curve is fetched on demand (curves are immutable
// once committed), so the ~5 KB-per-shot curve bulk never rides the
// snapshot copy.
struct ShotStatsRecordsSnapshot {
  ShotStatsView stats{};
  ShotLogRecord records[SHOT_LOG_CAPACITY]{};
  size_t count = 0;
  uint32_t epoch = 0;
};

// Closes the Stats aggregate object in `out` with "recent": the shots behind
// the averages, newest first, in the same eligible window as
// ShotLog::statsView, so the Stats charts never depend on the table's sort or
// scroll. Compact [id,presetId,arrivalS,verdict,fastBeforeS,slowAfterS] arrays
// (at most ~51 bytes each) keep the first frame, which also carries a full
// row, small. Fails, without a cut array, when `out` does not end in "}" or
// the list does not fit.
inline bool appendStatsRecent(const ShotStatsRecordsSnapshot &snapshot, char *out, size_t cap) {
  size_t used = strlen(out);
  if (!used || out[used - 1] != '}') return false;
  --used;  // Reopen the object over its closing brace.
  auto put = [&](int n) { return n >= 0 && (used += static_cast<size_t>(n)) < cap; };
  bool ok = put(snprintf(out + used, cap - used, "%s\"recent\":[", used > 1 ? "," : ""));
  for (size_t i = 0, found = 0; ok && i < snapshot.count && found < SHOT_LOG_STATS_WINDOW; ++i) {
    const ShotLogRecord &record = snapshot.records[i];
    if (!shotLogRecordEligible(record)) continue;
    char arrival[16] = "null";
    if (record.arrivalDs != SHOT_LOG_METRIC_MISSING)
      snprintf(arrival, sizeof(arrival), "%.1f", record.arrivalDs / 10.0);
    ok = put(snprintf(out + used, cap - used, "%s[%lu,%u,%s,%s,%.1f,%.1f]", found++ ? "," : "",
                      static_cast<unsigned long>(record.id), static_cast<unsigned>(shotLogPresetId(record)),
                      arrival, shotLogVerdictJson(record.verdict), record.fastBeforeDs / 10.0,
                      record.slowAfterDs / 10.0));
  }
  return ok && put(snprintf(out + used, cap - used, "]}"));
}

inline void resetShotCurveRecord(ShotCurveRecord &curve) {
  memset(&curve, 0, sizeof(curve));
  curve.atmClearedMs = SHOT_LOG_METRIC_MISSING;
  curve.firstDrop = missingShotCurveEvent();
  curve.extended = missingShotCurveEvent();
  curve.atm = missingShotCurveEvent();
  curve.ended = missingShotCurveEvent();
}

inline ShotCurveRecord emptyShotCurveRecord() {
  ShotCurveRecord curve;
  resetShotCurveRecord(curve);
  return curve;
}

static_assert(sizeof(ShotCurveRecord) == 5132,
              "ShotCurveRecord packing is part of the flash sidecar schema");

struct ShotCurveHeader {
  uint32_t magic;
  uint16_t schemaVersion;
  uint16_t recordSize;
  uint32_t generation;
  uint16_t count;
  uint16_t writeIndex;
  uint32_t checksum;
};

static_assert(sizeof(ShotCurveHeader) == 20,
              "ShotCurveHeader packing is part of the flash sidecar schema");

// Live bank holds every retained curve; a persistence image holds only the
// small batch it is about to commit.
template <size_t Capacity>
struct ShotCurveRingStore {
  ShotCurveHeader header;
  ShotCurveRecord records[Capacity];
};
using ShotCurveStore = ShotCurveRingStore<SHOT_CURVE_CAPACITY>;

static_assert(sizeof(ShotCurveStore) ==
                  sizeof(ShotCurveHeader) +
                      sizeof(ShotCurveRecord) * SHOT_CURVE_CAPACITY,
              "ShotCurveStore packing must stay 4-byte aligned for chunked flash I/O");
static_assert(sizeof(ShotCurveStore) == 2566020,
              "ShotCurveStore packing is part of the flash sidecar schema");

inline uint32_t shotCurveChecksum(const ShotCurveStore &store) {
  uint32_t crc = crc32Update(
      0xFFFFFFFFU, reinterpret_cast<const uint8_t *>(&store.header),
      offsetof(ShotCurveHeader, checksum));
  if (store.header.count > 0) {
    crc = crc32Update(crc, reinterpret_cast<const uint8_t *>(store.records),
                      static_cast<size_t>(store.header.count) *
                          sizeof(ShotCurveRecord));
  }
  return ~crc;
}

inline void finalizeShotCurveStore(ShotCurveStore &store) {
  store.header.magic = SHOT_CURVE_MAGIC;
  store.header.schemaVersion = SHOT_CURVE_SCHEMA_VERSION;
  store.header.recordSize = sizeof(ShotCurveRecord);
  store.header.checksum = 0;
  store.header.checksum = shotCurveChecksum(store);
}

inline bool validShotCurveStore(const ShotCurveStore &store) {
  return store.header.magic == SHOT_CURVE_MAGIC &&
         store.header.schemaVersion == SHOT_CURVE_SCHEMA_VERSION &&
         store.header.recordSize == sizeof(ShotCurveRecord) &&
         store.header.count <= SHOT_CURVE_CAPACITY &&
         store.header.writeIndex < SHOT_CURVE_CAPACITY &&
         store.header.checksum == shotCurveChecksum(store);
}

inline void resetShotCurveStore(ShotCurveStore &store) {
  memset(&store, 0, sizeof(store));
  store.header.generation = 1;
  finalizeShotCurveStore(store);
}

// Pack the ring into records[0..count) (oldest first) in place, mirroring
// compactShotLogStore.
inline void compactShotCurveStore(ShotCurveStore &store) {
  store.header.writeIndex = compactRecordRing(
      store.records, SHOT_CURVE_CAPACITY, sizeof(ShotCurveRecord),
      store.header.count, store.header.writeIndex);
}

template <typename Curve>
inline bool appendShotCurveObservation(Curve &curve, float weight,
                                       uint32_t elapsed, bool split,
                                       bool held = false) {
  if (curve.truncated) return false;
  const int16_t cg = shotLogWeightToCentigrams(weight);
  if (shotLogWeightIsMissing(cg) ||
      (curve.count != 0 && elapsed < curve.atMs[curve.count - 1U])) return false;
  if (elapsed > SHOT_CURVE_MAX_TIME_MS || curve.count == SHOT_CURVE_MAX_POINTS) {
    curve.truncated = true;
    return false;
  }
  curve.weightCg[curve.count] = cg;
  curve.atMs[curve.count] = static_cast<uint16_t>(elapsed);
  if (split && curve.count != 0)
    curve.breakBefore[curve.count / 8U] |= static_cast<uint8_t>(1U << (curve.count % 8U));
  if (held) curve.held[curve.count / 8U] |= static_cast<uint8_t>(1U << (curve.count % 8U));
  ++curve.count;
  return true;
}

struct ShotCurveSampler {
  uint32_t startMs = 0;
  uint16_t count = 0;
  bool truncated = false;
  bool pendingBreak = false;
  bool active = false;
  uint16_t atmClearedMs = SHOT_LOG_METRIC_MISSING;
  ShotCurveEvent firstDrop = missingShotCurveEvent();
  ShotCurveEvent extended = missingShotCurveEvent();
  ShotCurveEvent atm = missingShotCurveEvent();
  ShotCurveEvent ended = missingShotCurveEvent();
  int16_t weightCg[SHOT_CURVE_MAX_POINTS] = {};
  uint16_t atMs[SHOT_CURVE_MAX_POINTS] = {};
  uint8_t breakBefore[SHOT_CURVE_BREAK_BYTES] = {};
  uint8_t held[SHOT_CURVE_BREAK_BYTES] = {};

  void reset(uint32_t startedAtMs) {
    startMs = startedAtMs;
    count = 0;
    truncated = false;
    pendingBreak = false;
    active = true;
    atmClearedMs = SHOT_LOG_METRIC_MISSING;
    firstDrop = missingShotCurveEvent();
    extended = missingShotCurveEvent();
    atm = missingShotCurveEvent();
    ended = missingShotCurveEvent();
    memset(weightCg, 0, sizeof(weightCg));
    memset(atMs, 0, sizeof(atMs));
    memset(breakBefore, 0, sizeof(breakBefore));
    memset(held, 0, sizeof(held));
  }

  bool elapsedMsFrom(uint32_t atMs, uint16_t &outMs) const {
    if (!active) {
      return false;
    }
    if (static_cast<int32_t>(atMs - startMs) < 0) {
      return false;
    }
    const uint32_t elapsedMs = atMs - startMs;
    if (elapsedMs >= UINT16_MAX) {
      return false;
    }
    outMs = static_cast<uint16_t>(elapsedMs);
    return true;
  }

  void latchEvent(ShotCurveEvent &event, uint32_t atMs, float weight) {
    if (shotCurveEventPresent(event) || !active) {
      return;
    }
    uint16_t ms = 0;
    if (!elapsedMsFrom(atMs, ms)) {
      return;
    }
    const int16_t cg = shotLogWeightToCentigrams(weight);
    if (shotLogWeightIsMissing(cg)) {
      return;
    }
    event.atMs = ms;
    event.weightCg = cg;
  }

  void latchFirstDrop(uint32_t atMs, float weight) {
    latchEvent(firstDrop, atMs, weight);
  }

  void latchExtended(uint32_t atMs, float weight) {
    latchEvent(extended, atMs, weight);
  }

  void latchAtm(uint32_t atMs, float weight) {
    if (shotCurveEventPresent(atm) &&
        atmClearedMs == SHOT_LOG_METRIC_MISSING) {
      return;
    }
    atm = missingShotCurveEvent();
    atmClearedMs = SHOT_LOG_METRIC_MISSING;
    latchEvent(atm, atMs, weight);
  }

  void latchAtmCleared(uint32_t atMs) {
    if (!shotCurveEventPresent(atm) ||
        atmClearedMs != SHOT_LOG_METRIC_MISSING) {
      return;
    }
    uint16_t ms = 0;
    if (!elapsedMsFrom(atMs, ms)) {
      return;
    }
    atmClearedMs = ms;
  }

  void markBreak() { if (active) pendingBreak = true; }

  // A pending break waits for the next accepted sample: held samples sit
  // beside the weight line, never start a segment.
  void accept(float weight, uint32_t receivedAtMs, bool isHeld = false) {
    if (!active || truncated) return;
    const bool appended = appendShotCurveObservation(
        *this, weight, receivedAtMs - startMs, pendingBreak && !isHeld, isHeld);
    pendingBreak = isHeld ? pendingBreak || !appended : !appended;
  }

  void captureEnd(uint32_t at, float weight = NAN) {
    if (!active) return;
    if (!std::isfinite(weight) && count != 0)
      weight = static_cast<float>(weightCg[count - 1U]) / 100.0f;
    latchEvent(ended, at, weight);
    active = false;
  }

  void snapshot(ShotCurveRecord &record, uint32_t shotId = 0) const {
    memset(&record, 0, sizeof(record));
    record.shotId = shotId;
    record.count = count;
    record.truncated = truncated;
    record.atmClearedMs = atmClearedMs;
    record.firstDrop = firstDrop;
    record.extended = extended;
    record.atm = atm;
    record.ended = ended;
    memcpy(record.weightCg, weightCg, sizeof(weightCg));
    memcpy(record.atMs, atMs, sizeof(atMs));
    memcpy(record.breakBefore, breakBefore, sizeof(breakBefore));
    memcpy(record.held, held, sizeof(held));
  }
};

// Post-drip yield changes only the endpoint annotation, never an observation.
inline bool settleShotCurveEndWeight(ShotCurveRecord &curve, float weight) {
  const int16_t cg = shotLogWeightToCentigrams(weight);
  if (!shotCurveEventPresent(curve.ended) || shotLogWeightIsMissing(cg))
    return false;
  curve.ended.weightCg = cg;
  return true;
}

inline bool shotCurveBreakBefore(const ShotCurveRecord &curve, size_t index) {
  return index < curve.count &&
         (curve.breakBefore[index / 8U] & (1U << (index % 8U))) != 0;
}

inline bool shotCurveHeld(const ShotCurveRecord &curve, size_t index) {
  return index < curve.count &&
         (curve.held[index / 8U] & (1U << (index % 8U))) != 0;
}

inline bool validShotCurveRecord(const ShotCurveRecord &curve) {
  if (curve.shotId == 0 || curve.count > SHOT_CURVE_MAX_POINTS) return false;
  for (size_t i = 0; i < curve.count; ++i)
    if (curve.atMs[i] > SHOT_CURVE_MAX_TIME_MS ||
        (i != 0 && curve.atMs[i] < curve.atMs[i - 1U]) ||
        shotLogWeightIsMissing(curve.weightCg[i])) return false;
  return true;
}

inline bool formatShotCurveMetricS(char *out, size_t capacity, const char *key,
                                   uint16_t ms) {
  if (out == nullptr || capacity < 8 || key == nullptr) {
    return false;
  }
  if (ms == SHOT_LOG_METRIC_MISSING) {
    return snprintf(out, capacity, ",\"%s\":null", key) > 0;
  }
  return snprintf(out, capacity, ",\"%s\":%.3f", key,
                  static_cast<double>(ms) / 1000.0) > 0;
}

inline bool formatShotCurveMetricCg(char *out, size_t capacity, const char *key,
                                    int16_t cg) {
  if (out == nullptr || capacity < 8 || key == nullptr) {
    return false;
  }
  if (shotLogWeightIsMissing(cg)) {
    return snprintf(out, capacity, ",\"%s\":null", key) > 0;
  }
  return snprintf(out, capacity, ",\"%s\":%d", key, static_cast<int>(cg)) > 0;
}

// JSON object body: paired actual-time weights, continuity, completeness/events.
inline bool formatShotCurveJsonBody(char *out, size_t capacity,
                                    const ShotCurveRecord &curve,
                                    uint16_t start = 0) {
  if (out == nullptr || capacity < 32 || curve.count > SHOT_CURVE_MAX_POINTS || start > curve.count) {
    return false;
  }
  size_t used = 0;
  auto append = [&](const char *text) -> bool {
    const size_t length = strlen(text);
    if (used + length + 1U > capacity) {
      return false;
    }
    memcpy(out + used, text, length);
    used += length;
    out[used] = '\0';
    return true;
  };
  if (!append("\"wCg\":[")) {
    return false;
  }
  const uint16_t count = curve.count;
  for (uint16_t i = start; i < count; ++i) {
    char item[12] = {};
    snprintf(item, sizeof(item), "%s%d", i == start ? "" : ",",
             static_cast<int>(curve.weightCg[i]));
    if (!append(item)) {
      return false;
    }
  }
  char piece[40] = {};
  if (!append("],\"wAtMs\":[")) return false;
  for (size_t i = start; i < count; ++i) {
    snprintf(piece, sizeof(piece), "%s%u", i == start ? "" : ",",
             static_cast<unsigned>(curve.atMs[i]));
    if (!append(piece)) return false;
  }
  if (!append("],\"wBreakBefore\":[")) return false;
  bool first = true;
  for (size_t i = start > 1 ? start : 1; i < count; ++i) {
    if (!shotCurveBreakBefore(curve, i)) continue;
    snprintf(piece, sizeof(piece), "%s%u", first ? "" : ",",
             static_cast<unsigned>(i));
    if (!append(piece)) return false;
    first = false;
  }
  if (!append("],\"wHeld\":[")) return false;
  first = true;
  for (size_t i = start; i < count; ++i) {
    if (!shotCurveHeld(curve, i)) continue;
    snprintf(piece, sizeof(piece), "%s%u", first ? "" : ",",
             static_cast<unsigned>(i));
    if (!append(piece)) return false;
    first = false;
  }
  snprintf(piece, sizeof(piece), "],\"wTruncated\":%s",
           curve.truncated ? "true" : "false");
  if (!append(piece)) return false;
  auto appendMetricS = [&](const char *key, uint16_t ms) -> bool {
    return formatShotCurveMetricS(piece, sizeof(piece), key, ms) &&
           append(piece);
  };
  auto appendMetricCg = [&](const char *key, int16_t cg) -> bool {
    return formatShotCurveMetricCg(piece, sizeof(piece), key, cg) &&
           append(piece);
  };
  return appendMetricS("dropS", curve.firstDrop.atMs) &&
         appendMetricCg("dropCg", curve.firstDrop.weightCg) &&
         appendMetricS("extendedS", curve.extended.atMs) &&
         appendMetricCg("extCg", curve.extended.weightCg) &&
         appendMetricS("atmS", curve.atm.atMs) &&
         appendMetricCg("atmCg", curve.atm.weightCg) &&
         appendMetricS("atmClearedS", curve.atmClearedMs) &&
         appendMetricS("endS", curve.ended.atMs) &&
         appendMetricCg("endCg", curve.ended.weightCg);
}

inline void copyShotCurveRecordToStatusFields(
    const ShotCurveRecord &curve, uint16_t &count, bool &truncated,
    uint16_t &firstDropMs, int16_t &firstDropCg, uint16_t &extendedMs,
    int16_t &extendedCg, uint16_t &atmMs, int16_t &atmCg,
    uint16_t &atmClearedMs, uint16_t &endedMs, int16_t &endedCg,
    int16_t *weightCg, size_t weightCapacity,
    uint16_t *atMs, uint8_t *breakBefore, uint8_t *held) {
  count = curve.count;
  truncated = curve.truncated;
  firstDropMs = curve.firstDrop.atMs;
  firstDropCg = curve.firstDrop.weightCg;
  extendedMs = curve.extended.atMs;
  extendedCg = curve.extended.weightCg;
  atmMs = curve.atm.atMs;
  atmCg = curve.atm.weightCg;
  atmClearedMs = curve.atmClearedMs;
  endedMs = curve.ended.atMs;
  endedCg = curve.ended.weightCg;
  if (weightCg == nullptr) {
    return;
  }
  const size_t copy =
      weightCapacity < SHOT_CURVE_MAX_POINTS ? weightCapacity
                                             : SHOT_CURVE_MAX_POINTS;
  memcpy(weightCg, curve.weightCg, copy * sizeof(int16_t));
  memcpy(atMs, curve.atMs, copy * sizeof(uint16_t));
  memcpy(breakBefore, curve.breakBefore, SHOT_CURVE_BREAK_BYTES);
  memcpy(held, curve.held, SHOT_CURVE_BREAK_BYTES);
}

inline void shotCurveRecordFromStatusFields(
    ShotCurveRecord &curve, uint16_t count, bool truncated, uint16_t firstDropMs, int16_t firstDropCg,
    uint16_t extendedMs, int16_t extendedCg, uint16_t atmMs, int16_t atmCg,
    uint16_t atmClearedMs, uint16_t endedMs, int16_t endedCg,
    const int16_t *weightCg, size_t weightCount,
    const uint16_t *atMs, const uint8_t *breakBefore, const uint8_t *held) {
  resetShotCurveRecord(curve);
  // Clamp at the boundary: weightCg only holds SHOT_CURVE_MAX_POINTS samples,
  // and count arrives from Web-UI status input.
  curve.count = count > SHOT_CURVE_MAX_POINTS ? SHOT_CURVE_MAX_POINTS : count;
  curve.truncated = truncated;
  curve.atmClearedMs = atmClearedMs;
  curve.firstDrop.atMs = firstDropMs;
  curve.firstDrop.weightCg = firstDropCg;
  curve.extended.atMs = extendedMs;
  curve.extended.weightCg = extendedCg;
  curve.atm.atMs = atmMs;
  curve.atm.weightCg = atmCg;
  curve.ended.atMs = endedMs;
  curve.ended.weightCg = endedCg;
  if (weightCg != nullptr && weightCount > 0) {
    const size_t copy =
        weightCount < SHOT_CURVE_MAX_POINTS ? weightCount
                                            : SHOT_CURVE_MAX_POINTS;
    memcpy(curve.weightCg, weightCg, copy * sizeof(int16_t));
    memcpy(curve.atMs, atMs, copy * sizeof(uint16_t));
    memcpy(curve.breakBefore, breakBefore, SHOT_CURVE_BREAK_BYTES);
    memcpy(curve.held, held, SHOT_CURVE_BREAK_BYTES);
  }
}

}  // namespace shotstopper
