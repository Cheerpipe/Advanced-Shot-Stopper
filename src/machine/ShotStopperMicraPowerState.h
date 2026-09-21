#pragma once

#include "ShotStopperLineaMicraTypes.h"
#include "ShotStopperMicraTiming.h"

namespace shotstopper {

// One optimistic overlay at a time, in either direction: the paddle wake
// gesture asserts ON before the cloud agrees, and an accepted scale-shutdown
// command asserts OFF before the next dashboard read confirms it. The overlay
// changes only the effective state and observation quality; the confirmed
// classification and its sample stay untouched until an authoritative read
// started after the overlay accepts, matched by generation.
enum class OptimisticDirection : uint8_t { NONE, ON, OFF };

class LineaMicraPowerStateTracker {
 public:
  LineaMicraStatus effectiveStatus(const LineaMicraStatus &authoritative,
                                   bool observing, uint32_t now) const {
    LineaMicraStatus result = authoritative;
    if (!observing || result.sampleAtMs == 0) {
      result.powerState = LineaMicraPowerState::UNKNOWN;
      result.effectiveOn = true;
    } else if (direction_ != OptimisticDirection::NONE &&
        static_cast<uint32_t>(now - optimisticAtMs_) <
            micra_timing::kOptimisticOverlayMs) {
      result.quality = LineaMicraObservationQuality::OPTIMISTIC;
      result.effectiveOn = direction_ == OptimisticDirection::ON;
      result.optimisticOn = direction_ == OptimisticDirection::ON;
      result.optimisticOff = direction_ == OptimisticDirection::OFF;
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
    return arm(authoritative, observing, OptimisticDirection::ON, now);
  }

  // A scale shutdown only asserts optimistic OFF once the cloud command was
  // accepted and the confirmed state is a fresh ON: the machine is certainly
  // heading to standby, but the dashboard has not said so yet.
  bool noteStandbyCommandAccepted(const LineaMicraStatus &authoritative,
                                  bool observing, uint32_t now) {
    return arm(authoritative, observing, OptimisticDirection::OFF, now);
  }

  uint32_t generation() const { return generation_; }

  bool acceptAuthoritative(uint32_t observationGeneration) {
    if (observationGeneration != generation_) return false;
    direction_ = OptimisticDirection::NONE;
    return true;
  }

  void reset() {
    direction_ = OptimisticDirection::NONE;
    ++generation_;
  }

 private:
  bool arm(const LineaMicraStatus &authoritative, bool observing,
           OptimisticDirection direction, uint32_t now) {
    const LineaMicraStatus before =
        effectiveStatus(authoritative, observing, now);
    const LineaMicraPowerState required =
        direction == OptimisticDirection::ON
            ? LineaMicraPowerState::OFF
            : LineaMicraPowerState::ON;
    if (before.powerState != required ||
        before.quality != LineaMicraObservationQuality::CURRENT)
      return false;
    direction_ = direction;
    optimisticAtMs_ = now;
    ++generation_;
    return true;
  }

  uint32_t generation_ = 0;
  uint32_t optimisticAtMs_ = 0;
  OptimisticDirection direction_ = OptimisticDirection::NONE;
};

}  // namespace shotstopper
