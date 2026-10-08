#pragma once

#include <stdint.h>
#include <string.h>
#include <cmath>

namespace shotstopper {

// LINEAR_EWMA is the only cutoff algorithm. Numeric value 1 matches the
// pre-removal persisted encoding; blobs storing 0 (removed regression mode)
// are aliased to 1 by the settings-load repair path.
enum class BbwAlgorithm : uint8_t { LINEAR_EWMA = 1 };
constexpr uint8_t DEFAULT_BBW_EWMA_ALPHA = 30;  // Hundredths, dimensionless.
// v1 was the removed regression policy; stored shot logs keep whatever
// version they were written with and are never recomputed.
constexpr uint8_t BBW_PROFILE_VERSION = 2;
constexpr uint8_t BBW_ALPHA_CANDIDATES[] = {10, 30, 50, 100};

inline bool validBbwAlpha(uint8_t alpha) {
  return alpha >= 1 && alpha <= 100;
}

inline uint8_t bbwAlgorithmVersion(uint8_t algorithm) {
  (void)algorithm;
  return BBW_PROFILE_VERSION;
}

inline bool parseBbwAlphaBaseline(double value, uint8_t &out) {
  if (!(value >= 0.01 && value <= 1.0)) return false;
  const unsigned hundredths = static_cast<unsigned>(value * 100.0 + 0.5);
  if (fabs(value * 100.0 - hundredths) > 1e-9) return false;
  out = static_cast<uint8_t>(hundredths);
  return true;
}

inline const char *bbwAlgorithmName(uint8_t algorithm) {
  return algorithm == static_cast<uint8_t>(BbwAlgorithm::LINEAR_EWMA)
             ? "linear_ewma"
             : "unknown";
}

inline bool parseBbwAlgorithm(const char *name, uint8_t &algorithm) {
  if (name == nullptr) return false;
  // "legacy" (the removed regression mode) stays accepted so old clients and
  // scripts keep working; both names select the only remaining algorithm.
  if (strcmp(name, "linear_ewma") == 0 || strcmp(name, "legacy") == 0) {
    algorithm = static_cast<uint8_t>(BbwAlgorithm::LINEAR_EWMA);
    return true;
  }
  return false;
}

}  // namespace shotstopper
