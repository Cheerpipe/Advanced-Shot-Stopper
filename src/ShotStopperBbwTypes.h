#pragma once

#include <stdint.h>
#include <string.h>
#include <cmath>

namespace shotstopper {

enum class BbwAlgorithm : uint8_t { LEGACY = 0, LINEAR_EWMA = 1 };
constexpr uint8_t DEFAULT_BBW_EWMA_ALPHA = 30;  // Hundredths, dimensionless.
constexpr uint8_t BBW_PROFILE_VERSION = 2;  // EWMA policy; regression remains v1.
constexpr uint8_t BBW_ALPHA_CANDIDATES[] = {10, 30, 50, 100};

inline bool validBbwAlpha(uint8_t alpha) {
  return alpha >= 1 && alpha <= 100;
}

inline uint8_t bbwAlgorithmVersion(uint8_t algorithm) {
  return algorithm == 0 ? 1 : BBW_PROFILE_VERSION;
}

inline bool parseBbwAlphaBaseline(double value, uint8_t &out) {
  if (!(value >= 0.01 && value <= 1.0)) return false;
  const unsigned hundredths = static_cast<unsigned>(value * 100.0 + 0.5);
  if (fabs(value * 100.0 - hundredths) > 1e-9) return false;
  out = static_cast<uint8_t>(hundredths);
  return true;
}

inline const char *bbwAlgorithmName(uint8_t algorithm) {
  switch (static_cast<BbwAlgorithm>(algorithm)) {
    case BbwAlgorithm::LEGACY: return "legacy";
    case BbwAlgorithm::LINEAR_EWMA: return "linear_ewma";
  }
  return "unknown";
}

inline bool parseBbwAlgorithm(const char *name, uint8_t &algorithm) {
  for (uint8_t value = 0; value <= 1; ++value) {
    if (name != nullptr && strcmp(name, bbwAlgorithmName(value)) == 0) {
      algorithm = value;
      return true;
    }
  }
  return false;
}

}  // namespace shotstopper
