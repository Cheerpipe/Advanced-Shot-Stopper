#pragma once

#include "ShotStopperBbwLegacy.h"
#include "ShotStopperBbwEwma.h"

namespace shotstopper {

inline float predictedWeightStopTimeS(const float *timeS, const float *weightG,
                                      size_t datapoints, float targetWeightG,
                                      float fallbackEndS,
                                      uint8_t algorithm = 0) {
  switch (static_cast<BbwAlgorithm>(algorithm)) {
    case BbwAlgorithm::LEGACY:
      return bbwLegacy::predict(timeS, weightG, datapoints, targetWeightG, fallbackEndS);
    case BbwAlgorithm::LINEAR_EWMA:
      return bbwEwma::predict(timeS, weightG, datapoints, targetWeightG, fallbackEndS);
  }
  return fallbackEndS;
}

inline bool learnBbwOffset(uint8_t algorithm, float offsetG, float finalWeightG,
                           float goalG, uint8_t alpha, bool acceptedFresh,
                           float &updatedOffset) {
  switch (static_cast<BbwAlgorithm>(algorithm)) {
    case BbwAlgorithm::LEGACY:
      return bbwLegacy::learn(offsetG, finalWeightG, goalG, updatedOffset);
    case BbwAlgorithm::LINEAR_EWMA:
      return bbwEwma::learn(offsetG, finalWeightG, goalG, alpha, acceptedFresh,
                            updatedOffset);
  }
  return false;
}

}  // namespace shotstopper
