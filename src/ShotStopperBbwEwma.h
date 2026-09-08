#pragma once

#include "ShotStopperDomain.h"
#include "ShotStopperScaleTypes.h"

namespace shotstopper {
namespace bbwEwma {

inline float clampOffset(float value) {
  return fmaxf(0.0f, fminf(MAX_OFFSET_G, value));
}

inline float predict(const float *timeS, const float *weightG, size_t count,
                     float targetG, float fallbackS) {
  if (!timeS || !weightG || count < WEIGHT_TREND_POINT_COUNT ||
      !isfinite(targetG) || !isfinite(fallbackS) ||
      weightG[count - 1] < WEIGHT_TREND_MIN_LAST_SAMPLE_G) return fallbackS;
  const size_t first = count - WEIGHT_TREND_POINT_COUNT;
  const float reference = timeS[count - 1];
  float meanX = 0.0f, meanY = 0.0f;
  for (size_t i = first; i < count; ++i) {
    if (!isfinite(timeS[i]) || !isfinite(weightG[i]) ||
        (i > first && timeS[i] <= timeS[i - 1])) return fallbackS;
    meanX += timeS[i] - reference;
    meanY += weightG[i];
  }
  meanX /= WEIGHT_TREND_POINT_COUNT;
  meanY /= WEIGHT_TREND_POINT_COUNT;
  float covariance = 0.0f, variance = 0.0f;
  for (size_t i = first; i < count; ++i) {
    const float dx = (timeS[i] - reference) - meanX;
    covariance += dx * (weightG[i] - meanY);
    variance += dx * dx;
  }
  // Legacy denominator = n * centered sum of squared seconds.
  if (!isfinite(variance) || variance < 0.000001f / WEIGHT_TREND_POINT_COUNT)
    return fallbackS;
  const float slope = covariance / variance;
  if (!isfinite(slope) || slope <= 0.0f) return fallbackS;
  const float predicted = reference + meanX + (targetG - meanY) / slope;
  return isfinite(predicted) &&
                 predicted >= reference + WEIGHT_TREND_MIN_HORIZON_S
             ? predicted : fallbackS;
}

inline bool learn(float offsetG, float finalWeightG, float goalG, uint8_t alpha,
                  bool acceptedFreshObservation, float &updatedOffset) {
  const float observation = offsetG + (finalWeightG - goalG);
  if (!acceptedFreshObservation || !validBbwAlpha(alpha) ||
      !isfinite(offsetG) || !isfinite(observation) ||
      fabsf(observation) > MAX_OFFSET_G) return false;
  updatedOffset = clampOffset(offsetG + (alpha / 100.0f) * (finalWeightG - goalG));
  return true;
}

// V2: replay bounded observations from the predictions before the oldest one.
// The fifth trajectory compares a custom incumbent; challengers stay unchanged.
struct Evidence {
  static constexpr uint8_t WINDOW = 20;
  float anchors[5] = {};
  float observations[WINDOW] = {};
  uint8_t initialAlpha = 0;  // Seeded by the first eligible observation.
  uint8_t count = 0;
  uint8_t position = 0;
  uint8_t cadence = 0;
  uint8_t sinceSwitch = 0;
  uint8_t challenger = 0;
  uint8_t wins = 0;

  uint8_t observe(float observation, float seed, uint8_t alpha) {
    if (!isfinite(observation) || fabsf(observation) > MAX_OFFSET_G ||
        !isfinite(seed) || !validBbwAlpha(alpha)) return alpha;
    if (count == 0) {
      for (float &anchor : anchors) anchor = seed;
      initialAlpha = alpha;
    }
    if (count == WINDOW) {
      for (uint8_t i = 0; i < 5; ++i) {
        const uint8_t gain = i < 4 ? BBW_ALPHA_CANDIDATES[i] : initialAlpha;
        anchors[i] = clampOffset(anchors[i] +
            (gain / 100.0f) * (observations[position] - anchors[i]));
      }
    }
    observations[position] = observation;
    position = (position + 1) % WINDOW;
    if (count < WINDOW) ++count;
    if (sinceSwitch < 10) ++sinceSwitch;
    cadence = (cadence + 1) % 5;
    if (count < WINDOW || cadence != 0) return alpha;
    float scores[5] = {};
    uint8_t incumbent = 4;
    for (uint8_t i = 0; i < 5; ++i) {
      const uint8_t gain = i < 4 ? BBW_ALPHA_CANDIDATES[i] : initialAlpha;
      float prediction = anchors[i];
      for (uint8_t j = 0; j < WINDOW; ++j) {
        const float error = observations[(position + j) % WINDOW] - prediction;
        scores[i] += error * error;  // Score before updating.
        prediction = clampOffset(prediction + (gain / 100.0f) * error);
      }
      if (!isfinite(scores[i])) { wins = 0; return alpha; }
      if (i < 4 && gain == alpha) incumbent = i;
    }
    uint8_t best = incumbent;
    bool tied = false;
    for (uint8_t i = 0; i < 4; ++i) {
      if (scores[i] < scores[best]) { best = i; tied = false; }
      else if (i != best && scores[i] == scores[best]) tied = true;
    }
    if (tied || best == incumbent || sinceSwitch < 10 ||
        !(scores[best] <= scores[incumbent] * 0.90f)) {
      wins = 0;
      return alpha;
    }
    wins = challenger == best ? wins + 1 : 1;
    challenger = best;
    if (wins < 2) return alpha;
    wins = 0;
    sinceSwitch = 0;
    return BBW_ALPHA_CANDIDATES[best];
  }
};
static_assert(sizeof(Evidence) <= 352, "BBW candidate RAM budget");

}  // namespace bbwEwma
}  // namespace shotstopper
