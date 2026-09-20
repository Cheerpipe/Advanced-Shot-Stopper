#pragma once

#include "ShotStopperLineaMicraTypes.h"
#include "ShotStopperMicraTiming.h"

namespace shotstopper {

class LineaMicraPowerStateTracker {
 public:
  LineaMicraStatus effectiveStatus(const LineaMicraStatus &authoritative,
                                   bool observing, uint32_t now) const {
    LineaMicraStatus result = authoritative;
    if (observing && optimisticOn_ &&
        static_cast<uint32_t>(now - optimisticOnAtMs_) <
            micra_timing::kOptimisticOnMs) {
      result.powerState = LineaMicraPowerState::ON;
      result.quality = LineaMicraObservationQuality::OPTIMISTIC;
      result.effectiveOn = true;
      result.optimisticOn = true;
    } else if (!observing || result.sampleAtMs == 0 ||
               static_cast<uint32_t>(now - result.sampleAtMs) >=
                   micra_timing::kStateFreshnessMs) {
      result.powerState = LineaMicraPowerState::UNKNOWN;
      result.effectiveOn = true;
      if (observing && result.sampleAtMs != 0) {
        result.quality = LineaMicraObservationQuality::STALE;
      }
    }
    return result;
  }

  bool notePhysicalStart(const LineaMicraStatus &authoritative, bool observing,
                         bool recognizeWake, uint32_t now) {
    const LineaMicraStatus before =
        effectiveStatus(authoritative, observing, now);
    if (before.powerState != LineaMicraPowerState::OFF) return false;
    optimisticOn_ = true;
    optimisticOnAtMs_ = now;
    ++generation_;
    return recognizeWake;
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
