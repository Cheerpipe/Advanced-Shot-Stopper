#pragma once

#include <stdint.h>

namespace shotstopper {

constexpr uint8_t RESET_HISTORY_CAPACITY = 10;

struct ResetHistoryEntry {
  uint32_t reasonCode = 0;
  uint32_t uptimeMs = 0;
};

// FNV-style mix over the history entries, seeded per persisted record. The
// byte-exact mix order is part of on-boot checksum validation; compute
// persisted checksums only through this helper. Takes a volatile pointer so
// callers reading RTC/NOINIT memory keep volatile semantics.
inline uint32_t resetHistoryChecksum(const volatile ResetHistoryEntry *history,
                                     uint32_t count, uint32_t seed) {
  uint32_t checksum = seed;
  for (uint32_t i = 0; i < count; ++i) {
    checksum = (checksum * 16777619UL) ^ history[i].reasonCode;
    checksum = (checksum * 16777619UL) ^ history[i].uptimeMs;
  }
  return checksum;
}

}  // namespace shotstopper
