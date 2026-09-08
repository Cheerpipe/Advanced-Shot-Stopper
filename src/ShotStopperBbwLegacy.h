#pragma once

#include "ShotStopperDomain.h"
#include "ShotStopperScaleTypes.h"

namespace shotstopper {
namespace bbwLegacy {

inline float predict(const float *timeS, const float *weightG,
                     size_t datapoints, float targetWeightG,
                     float fallbackEndS) {
  if (!isfinite(targetWeightG) || !isfinite(fallbackEndS)) {
    return fallbackEndS;
  }
  const WeightTrendFit fit = fitWeightTrend(timeS, weightG, datapoints);
  if (!fit.valid) {
    return fallbackEndS;
  }
  const float predicted = (targetWeightG - fit.intercept) / fit.slope;
  const float latestSampleS = timeS[datapoints - 1];
  if (!isfinite(predicted) ||
      predicted < latestSampleS + WEIGHT_TREND_MIN_HORIZON_S) {
    return fallbackEndS;
  }
  return predicted;
}

inline bool learn(float offsetG, float finalWeightG, float goalG,
                  float &updatedOffset) {
  const float observedError = finalWeightG - goalG + offsetG;
  if (fabsf(observedError) > MAX_OFFSET_G) return false;
  updatedOffset = offsetG + finalWeightG - goalG;
  if (!isfinite(updatedOffset)) return false;
  if (updatedOffset < 0.0f) updatedOffset = 0.0f;
  else if (updatedOffset > MAX_OFFSET_G) updatedOffset = MAX_OFFSET_G;
  return true;
}

}  // namespace bbwLegacy
}  // namespace shotstopper
