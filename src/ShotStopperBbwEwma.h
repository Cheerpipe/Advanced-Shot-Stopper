#pragma once

#include "ShotStopperDomain.h"
#include "ShotStopperScaleTypes.h"

namespace shotstopper {
namespace bbwEwma {

inline float clampOffset(float value, float minG = 0.0f) {
  return fmaxf(minG, fminf(MAX_OFFSET_G, value));
}

inline bool learn(float offsetG, float finalWeightG, float goalG, uint8_t alpha,
                  bool acceptedFreshObservation, float &updatedOffset) {
  const float observation = offsetG + (finalWeightG - goalG);
  if (!acceptedFreshObservation || !validBbwAlpha(alpha) ||
      !std::isfinite(offsetG) || !std::isfinite(observation) ||
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
    if (!std::isfinite(observation) || fabsf(observation) > MAX_OFFSET_G ||
        !std::isfinite(seed) || !validBbwAlpha(alpha)) return alpha;
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
      if (!std::isfinite(scores[i])) { wins = 0; return alpha; }
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
