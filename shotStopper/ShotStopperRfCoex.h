#pragma once

#include <stdint.h>
#include <atomic>

#if !defined(SHOT_STOPPER_HOST_TEST)
#if __has_include(<esp_coexist.h>)
#include <esp_coexist.h>
#define SHOT_STOPPER_RF_COEX_HAS_IDF 1
#endif
#endif

namespace shotstopper {

// Always PREFER_BT. Scale scan, GATT, and brew share one 2.4 GHz radio with
// STA; a flat BT preference keeps discovery and the weight stream from losing
// airtime to beacons. STA associate may take longer. There is no IDF getter
// for the live preference.

enum class RfCoexPreference : uint8_t {
  BT = 2,
  UNKNOWN = 255
};

inline const char *rfCoexPreferenceName(RfCoexPreference preference) {
  switch (preference) {
    case RfCoexPreference::BT:
      return "BT";
    case RfCoexPreference::UNKNOWN:
      return "UNKNOWN";
  }
  return "UNKNOWN";
}

namespace detail {
inline std::atomic<uint8_t> g_rfCoexApplied{
    static_cast<uint8_t>(RfCoexPreference::UNKNOWN)};
inline std::atomic<int32_t> g_rfCoexLastError{0};
inline std::atomic<uint32_t> g_rfCoexFailures{0};
}  // namespace detail

inline RfCoexPreference snapshotRfCoexPreference() {
  return static_cast<RfCoexPreference>(
      detail::g_rfCoexApplied.load(std::memory_order_acquire));
}

inline int32_t rfCoexLastError() {
  return detail::g_rfCoexLastError.load(std::memory_order_relaxed);
}

inline uint32_t rfCoexFailureCount() {
  return detail::g_rfCoexFailures.load(std::memory_order_relaxed);
}

inline bool ensureRfCoexBt() {
#if defined(SHOT_STOPPER_RF_COEX_HAS_IDF)
  const esp_err_t result = esp_coex_preference_set(ESP_COEX_PREFER_BT);
  detail::g_rfCoexLastError.store(static_cast<int32_t>(result),
                                  std::memory_order_relaxed);
  if (result != ESP_OK) {
    detail::g_rfCoexFailures.fetch_add(1, std::memory_order_relaxed);
    detail::g_rfCoexApplied.store(
        static_cast<uint8_t>(RfCoexPreference::UNKNOWN),
        std::memory_order_release);
    return false;
  }
#endif
  detail::g_rfCoexLastError.store(0, std::memory_order_relaxed);
  detail::g_rfCoexApplied.store(static_cast<uint8_t>(RfCoexPreference::BT),
                                std::memory_order_release);
  return true;
}

}  // namespace shotstopper
