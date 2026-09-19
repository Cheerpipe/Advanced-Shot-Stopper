#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace shotstopper::micra_timing {

// Product-policy defaults pending hardware qualification. The service measures
// monotonic elapsed time; scale-critical work suspends admission. Changes need
// a firmware rebuild plus scheduler, coexistence and timeout test review.
inline constexpr std::array<uint32_t, 3> kRetryDelaysMs{{3000, 6000, 9000}};
inline constexpr size_t kMaxAttempts = 1U + kRetryDelaysMs.size();
inline constexpr uint32_t kStatePollConnectedIdleMs = 15000;
inline constexpr uint32_t kStatePollNoScaleMs = 15000;
inline constexpr uint32_t kStateFreshnessMs = 30000;
inline constexpr uint32_t kExhaustedCooldownMs = 60000;
inline constexpr uint32_t kPaddleAssumedOnMs = 30000;
inline constexpr uint32_t kJitterMaxMs = 1000;
inline constexpr uint32_t kMinDisconnectedMs = 1000;
inline constexpr uint32_t kDiscoverySliceMs = 2000;
inline constexpr uint32_t kConnectTimeoutMs = 3000;
inline constexpr uint32_t kAttTimeoutMs = 1000;
inline constexpr uint32_t kConnectedSessionMaxMs = 6000;
inline constexpr uint32_t kRequestDeadlineMs = 90000;
inline constexpr uint32_t kResultReadIntervalMs = 250;
inline constexpr size_t kResultReadMaxCount = 4;

static_assert(kMaxAttempts <= UINT8_MAX, "attempt count must fit request state");
static_assert(kRetryDelaysMs[0] > 0 &&
                  kRetryDelaysMs[0] < kRetryDelaysMs[1] &&
                  kRetryDelaysMs[1] < kRetryDelaysMs[2],
              "retry delays must be positive and increasing");
static_assert(kStatePollConnectedIdleMs > 0 && kStatePollNoScaleMs > 0 &&
                  kStateFreshnessMs > 0 && kMinDisconnectedMs > 0,
              "polling and disconnect opportunities must be positive");
static_assert(kConnectTimeoutMs > 0 && kAttTimeoutMs > 0 &&
                  kConnectedSessionMaxMs > kAttTimeoutMs &&
                  kRequestDeadlineMs > kRetryDelaysMs.back(),
              "transport timeouts must fit within the request deadline");

}  // namespace shotstopper::micra_timing
