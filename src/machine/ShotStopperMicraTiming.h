#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace shotstopper::micra_timing {

inline constexpr std::array<uint32_t, 3> kRetryDelaysMs{{3000, 6000, 9000}};
inline constexpr size_t kMaxAttempts = 1U + kRetryDelaysMs.size();
inline constexpr uint32_t kStatePollMs = 15000;
inline constexpr uint32_t kStateFreshnessMs = 30000;
inline constexpr uint32_t kOptimisticOnMs = 30000;
inline constexpr uint32_t kExhaustedCooldownMs = 60000;
inline constexpr uint32_t kHttpTimeoutMs = 10000;
inline constexpr uint32_t kRequestDeadlineMs = 60000;
inline constexpr uint32_t kAccessTokenLifetimeMs = 60U * 60U * 1000U;
inline constexpr uint32_t kAccessTokenRefreshAgeMs = 50U * 60U * 1000U;

inline constexpr bool accessTokenRefreshDue(uint32_t ageMs) {
  return ageMs >= kAccessTokenRefreshAgeMs;
}

static_assert(kRetryDelaysMs[0] < kRetryDelaysMs[1] &&
                  kRetryDelaysMs[1] < kRetryDelaysMs[2]);
static_assert(kStatePollMs > 0 && kStateFreshnessMs >= kStatePollMs);
static_assert(kOptimisticOnMs <= kStateFreshnessMs);
static_assert(kAccessTokenRefreshAgeMs < kAccessTokenLifetimeMs);

}  // namespace shotstopper::micra_timing
