#pragma once

#include <string.h>
#include "ShotStopperTimeZoneData.h"

namespace shotstopper {

inline const tzdb::Zone *findTimeZone(const char *id) {
  if (id == nullptr || *id == '\0') return nullptr;
  size_t low = 0, high = tzdb::kZoneCount;
  while (low < high) {
    const size_t mid = low + (high - low) / 2;
    const int order = strcmp(id, tzdb::kZones[mid].id);
    if (order == 0) return &tzdb::kZones[mid];
    if (order < 0) high = mid;
    else low = mid + 1;
  }
  return nullptr;
}

// The generated range is [2025, 2100); every schedule starts at minute zero.
// The next transition is returned as UTC seconds, or zero when none remains.
inline bool timeZoneOffsetAt(const char *id, uint32_t utcSec,
                             int16_t &offsetMinutes, uint32_t *nextUtc = nullptr) {
  if (utcSec < tzdb::kStartUtc || utcSec >= tzdb::kEndUtc) return false;
  const tzdb::Zone *zone = findTimeZone(id);
  if (zone == nullptr) return false;
  const uint32_t minute = (utcSec - tzdb::kStartUtc) / 60U;
  size_t low = zone->first, high = low + zone->count;
  while (low + 1 < high) {
    const size_t mid = low + (high - low) / 2;
    if (tzdb::kTransitions[mid].minute <= minute) low = mid;
    else high = mid;
  }
  offsetMinutes = tzdb::kTransitions[low].offset;
  if (nextUtc != nullptr) {
    const size_t next = low + 1;
    *nextUtc = next < zone->first + zone->count
                   ? tzdb::kStartUtc + tzdb::kTransitions[next].minute * 60U
                   : 0;
  }
  return true;
}

inline bool timeZoneOffsetAtOrUtc(const char *id, uint32_t utcSec,
                                  int16_t &offsetMinutes,
                                  uint32_t *nextUtc = nullptr) {
  return timeZoneOffsetAt(id != nullptr && id[0] != '\0' ? id : "Etc/UTC",
                          utcSec, offsetMinutes, nextUtc);
}

inline const char *timeZoneResolution(const char *id, uint32_t utcSec) {
  if (utcSec == 0) return "clock_unavailable";
  if (id == nullptr || id[0] == '\0') return "unconfigured";
  if (findTimeZone(id) == nullptr) return "unknown_zone";
  if (utcSec < tzdb::kStartUtc || utcSec >= tzdb::kEndUtc)
    return "rules_out_of_range";
  return "resolved";
}

}  // namespace shotstopper
