#pragma once

#include <stdint.h>
#include <string.h>

namespace shotstopper {

enum class BbwAlgorithm : uint8_t { LEGACY = 0, LINEAR_EWMA = 1 };
constexpr uint8_t DEFAULT_BBW_EWMA_ALPHA = 30;  // Hundredths, dimensionless.
constexpr uint8_t BBW_PROFILE_VERSION = 1;
constexpr uint8_t BBW_ALPHA_CANDIDATES[] = {10, 30, 50, 100};

inline bool validBbwAlpha(uint8_t alpha) {
  for (uint8_t candidate : BBW_ALPHA_CANDIDATES) {
    if (alpha == candidate) return true;
  }
  return false;
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
