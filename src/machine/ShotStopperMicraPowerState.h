#pragma once

#include "ShotStopperLineaMicraTypes.h"
#include "ShotStopperMicraTiming.h"

namespace shotstopper {

class LineaMicraPowerStateTracker {
 public:
  LineaMicraStatus effectiveStatus(const LineaMicraStatus &authoritative,
                                   bool observing, uint32_t now) const {
    LineaMicraStatus result = authoritative;
    if (!observing || result.sampleAtMs == 0) {
      result.powerState = LineaMicraPowerState::UNKNOWN;
      result.effectiveOn = true;
    } else if (optimisticOn_ &&
        static_cast<uint32_t>(now - optimisticOnAtMs_) <
            micra_timing::kOptimisticOnMs) {
      result.quality = LineaMicraObservationQuality::OPTIMISTIC;
      result.effectiveOn = true;
      result.optimisticOn = true;
    } else if (result.powerState != LineaMicraPowerState::UNKNOWN &&
               static_cast<uint32_t>(now - result.sampleAtMs) >=
                   micra_timing::kStateFreshnessMs) {
      result.quality = LineaMicraObservationQuality::STALE;
    }
    return result;
  }

  bool notePhysicalStart(const LineaMicraStatus &authoritative, bool observing,
                         bool recognizeWake, uint32_t now) {
    if (!recognizeWake) return false;
    const LineaMicraStatus before =
        effectiveStatus(authoritative, observing, now);
    if (before.powerState != LineaMicraPowerState::OFF ||
        before.quality != LineaMicraObservationQuality::CURRENT)
      return false;
    optimisticOn_ = true;
    optimisticOnAtMs_ = now;
    ++generation_;
    return true;
  }

  uint32_t generation() const { return generation_; }

  bool acceptAuthoritative(uint32_t observationGeneration) {
    if (observationGeneration != generation_) return false;
    optimisticOn_ = false;
    return true;
  }

  void reset() {
    optimisticOn_ = false;
    ++generation_;
  }

 private:
  uint32_t generation_ = 0;
  uint32_t optimisticOnAtMs_ = 0;
  bool optimisticOn_ = false;
};

}  // namespace shotstopper
