#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace shotstopper::micra_timing {

inline constexpr std::array<uint32_t, 3> kRetryDelaysMs{{3000, 6000, 9000}};
inline constexpr size_t kMaxAttempts = 1U + kRetryDelaysMs.size();
inline constexpr uint32_t kStatePollMs = 30000;
inline constexpr uint32_t kStateFreshnessMs = 30000;
inline constexpr uint32_t kOptimisticOnMs = 2U * kStatePollMs;
inline constexpr uint32_t kPostWakeObservationDelayMs = 15000;
inline constexpr uint32_t kExhaustedCooldownMs = 60000;
inline constexpr uint32_t kGateRetryMs = 1000;
inline constexpr uint32_t kHttpTimeoutMs = 10000;
inline constexpr uint32_t kRequestDeadlineMs = 60000;
inline constexpr uint32_t kAccessTokenLifetimeMs = 60U * 60U * 1000U;
inline constexpr uint32_t kAccessTokenRefreshAgeMs = 50U * 60U * 1000U;

inline constexpr bool accessTokenRefreshDue(uint32_t ageMs) {
  return ageMs >= kAccessTokenRefreshAgeMs;
}

inline constexpr bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

class ObservationSchedule {
 public:
  bool automaticDue(uint32_t now) const {
    return deadlineReached(now, automaticAtMs_) && observationAllowed(now);
  }

  bool observationAllowed(uint32_t now) const {
    return !postWakePending_ || deadlineReached(now, postWakeAtMs_);
  }

  void dueNow(uint32_t now) { automaticAtMs_ = now; }

  void armPostWake(uint32_t now) {
    postWakeAtMs_ = now + kPostWakeObservationDelayMs;
    postWakePending_ = true;
  }

  void observationStarted(uint32_t now) {
    if (postWakePending_ && deadlineReached(now, postWakeAtMs_)) {
      postWakePending_ = false;
    }
  }

  void scheduleNext(uint32_t now) {
    if (!postWakePending_) automaticAtMs_ = now + kStatePollMs;
  }

 private:
  uint32_t automaticAtMs_ = 0;
  uint32_t postWakeAtMs_ = 0;
  bool postWakePending_ = false;
};

static_assert(kRetryDelaysMs[0] < kRetryDelaysMs[1] &&
                  kRetryDelaysMs[1] < kRetryDelaysMs[2]);
static_assert(kStatePollMs > 0 && kStateFreshnessMs >= kStatePollMs);
static_assert(kOptimisticOnMs == 2U * kStatePollMs);
static_assert(kPostWakeObservationDelayMs < kOptimisticOnMs);
static_assert(kAccessTokenRefreshAgeMs < kAccessTokenLifetimeMs);

}  // namespace shotstopper::micra_timing
