#include "../ShotStopperTimeZone.h"

#include <cassert>
#include <cstring>

using namespace shotstopper;

int main() {
  static_assert(sizeof(tzdb::Transition) == 6);
  assert(std::strcmp(tzdb::kVersion, "2026d") == 0);
  assert(tzdb::kZoneCount == 597);
  int16_t offset = 0;
  uint32_t next = 0;
  assert(timeZoneOffsetAt("America/Santiago", 1775357999U, offset, &next));
  assert(offset == -180 && next == 1775358000U);
  assert(timeZoneOffsetAt("America/Santiago", 1775358000U, offset));
  assert(offset == -240);
  assert(timeZoneOffsetAt("America/New_York", 1772953199U, offset));
  assert(offset == -300);
  assert(timeZoneOffsetAt("US/Eastern", 1772953200U, offset));
  assert(offset == -240);
  assert(timeZoneOffsetAt("Asia/Kathmandu", 1772953200U, offset));
  assert(offset == 345);
  assert(timeZoneOffsetAt("Pacific/Chatham", 1772953200U, offset));
  assert(offset == 825);
  assert(timeZoneOffsetAt("Australia/Lord_Howe", 1772953200U, offset));
  assert(offset == 660);
  assert(timeZoneOffsetAt("Etc/UTC", 1735689600U, offset));
  assert(offset == 0);
  assert(!timeZoneOffsetAt("Invalid/Zone", 1772953200U, offset));
  assert(!timeZoneOffsetAt("Etc/UTC", 1735689599U, offset));
  assert(!timeZoneOffsetAt("Etc/UTC", 4102444800U, offset));
  for (const auto &zone : tzdb::kZones) {
    assert(zone.count > 0);
    assert(tzdb::kTransitions[zone.first].minute == 0);
    assert(timeZoneOffsetAt(zone.id, 1735689600U, offset));
    assert(timeZoneOffsetAt(zone.id, 4102444799U, offset));
  }
}
