#pragma once

#include "ShotStopperScaleTypes.h"

// =============================================================================
// LAYER: Cup (presence FSM)
// =============================================================================
// WHAT: PRESENT / ABSENT from weight hysteresis on the scale stream. Tare does
//       not change state. No heap; state is BSS.
//
// BOUNDARY: Scale-weight sensing only. Must not talk to machine, paddle,
// momentary, reed, or brew actuators. Emits CupPresenceEvent; the stopper
// decides whether REMOVED cuts the shot. No MachineType / paddle details.

namespace {

struct CupPresenceRuntime {
  CupPresenceState state = CupPresenceState::ABSENT;
  bool holdTransitions = false;
  bool inNegativeHole = false;
  bool removedArmed = false;
  // True after a tare while the cup is already PRESENT. 0 g then means "cup
  // tared on the pan", so removal requires the negative hole. Untared PRESENT
  // treats a stable empty pan (~0 g) as REMOVED.
  bool taredWhilePresent = false;
  bool referenceUncertain = false;
  uint8_t removedConfirmations = 0;
  uint8_t placeStabilitySamples = 0;
  uint32_t lastRemovedAtMs = 0;
  uint32_t lastRemovedPacketSequence = 0;
  uint32_t placeStabilityStartedAtMs = 0;
  uint32_t placeLastSampleAtMs = 0;
  uint32_t placementId = 0;
  float holeWeightG = 0.0f;
  float placeCandidateWeightG = 0.0f;
  float placeMinimumG = 0.0f;
  float placeMaximumG = 0.0f;
  float occupiedReferenceG = 0.0f;
  float occupiedMinimumG = 0.0f;
  float occupiedMaximumG = 0.0f;
  float occupiedPlacementThresholdG = 0.0f;
};

CupPresenceRuntime cupPresence;

void resetCupPlaceStabilityStreak() {
  cupPresence.placeStabilitySamples = 0;
  cupPresence.placeStabilityStartedAtMs = 0;
  cupPresence.placeLastSampleAtMs = 0;
  cupPresence.placeCandidateWeightG = 0.0f;
  cupPresence.placeMinimumG = 0.0f;
  cupPresence.placeMaximumG = 0.0f;
}

}  // namespace

CupPresenceState cupPresenceState() { return cupPresence.state; }
bool cupPresenceIsTared() {
  return cupPresence.taredWhilePresent && !cupPresence.referenceUncertain;
}
bool cupPresenceIsKnown() { return !cupPresence.referenceUncertain; }
float cupPresenceReferenceG() { return cupPresence.occupiedReferenceG; }
float cupPresenceMinimumG() { return cupPresence.occupiedMinimumG; }
float cupPresenceMaximumG() { return cupPresence.occupiedMaximumG; }
float cupPresencePlacementThresholdG() { return cupPresence.occupiedPlacementThresholdG; }
uint32_t cupPresencePlacementId() { return cupPresence.placementId; }

void restoreCupTareReference(bool previouslyTared, float previousReferenceG) {
  if (cupPresence.state == CupPresenceState::PRESENT) {
    cupPresence.taredWhilePresent = previouslyTared;
    cupPresence.occupiedReferenceG = previousReferenceG;
    cupPresence.referenceUncertain = false;
  }
}

void markCupTareReferenceUncertain() {
  if (cupPresence.state == CupPresenceState::PRESENT) {
    cupPresence.referenceUncertain = true;
  }
}

void resetCupSampleEvidence() {
  resetCupPlaceStabilityStreak();
  cupPresence.removedConfirmations = 0;
  cupPresence.lastRemovedAtMs = 0;
  cupPresence.lastRemovedPacketSequence = 0;
}

void resetCupPresence() {
  cupPresence = CupPresenceRuntime{};
}

void resyncCupPresenceIfPanEmpty(float weight) {
  if (cupPresence.state != CupPresenceState::PRESENT ||
      cupPresence.taredWhilePresent || cupPresence.referenceUncertain) {
    return;
  }
  if (!isfinite(weight) || weight >= runtimeConfig.minimumCupWeightG) {
    return;
  }
  cupPresence.state = CupPresenceState::ABSENT;
  cupPresence.taredWhilePresent = false;
  cupPresence.inNegativeHole = false;
  cupPresence.removedArmed = false;
  cupPresence.removedConfirmations = 0;
  cupPresence.lastRemovedAtMs = 0;
  cupPresence.lastRemovedPacketSequence = 0;
  cupPresence.holeWeightG = 0.0f;
  cupPresence.occupiedReferenceG = 0.0f;
  resetCupPlaceStabilityStreak();
}

void holdCupPresenceTransitions(bool hold) {
  cupPresence.holdTransitions = hold;
  resetCupPlaceStabilityStreak();
  cupPresence.removedConfirmations = 0;
  cupPresence.lastRemovedAtMs = 0;
  cupPresence.lastRemovedPacketSequence = 0;
}

void notifyCupPresenceTare() {
  if (cupPresence.state == CupPresenceState::PRESENT) {
    cupPresence.taredWhilePresent = true;
    cupPresence.occupiedReferenceG = 0.0f;
    cupPresence.referenceUncertain = false;
  }
  cupPresence.inNegativeHole = false;
  cupPresence.holeWeightG = 0.0f;
  resetCupPlaceStabilityStreak();
}

CupPresenceEvent feedCupPresence(float weight, uint32_t receivedAtMs,
                                 uint32_t packetSequence, bool allowPlacement = true) {
  if (!isfinite(weight)) {
    return CupPresenceEvent::NONE;
  }

  const float minCupG = runtimeConfig.minimumCupWeightG;
  const float removedG = runtimeConfig.cupRemovedWeightG;
  // A lighter put-back may remain below zero until tare. Its stationary
  // occupied plateau is not a second lift; preserve the additional drop.
  const float removalReferenceG = fminf(0.0f, cupPresence.occupiedReferenceG);
  const bool removalCandidate =
      weight <= removalReferenceG + removedG ||
      (!cupPresence.taredWhilePresent && !cupPresence.referenceUncertain &&
       weight < minCupG);

  if (!removalCandidate) {
    cupPresence.removedArmed = true;
    cupPresence.removedConfirmations = 0;
    cupPresence.lastRemovedAtMs = 0;
    cupPresence.lastRemovedPacketSequence = 0;
  }

  if (cupPresence.holdTransitions) {
    return CupPresenceEvent::NONE;
  }

  if (cupPresence.state == CupPresenceState::PRESENT) {
    if (!removalCandidate) {
      resetCupPlaceStabilityStreak();
      return CupPresenceEvent::NONE;
    }
    if (!cupPresence.removedArmed) {
      return CupPresenceEvent::NONE;
    }

    const bool consecutive = cupPresence.removedConfirmations > 0 &&
        static_cast<int32_t>(receivedAtMs - cupPresence.lastRemovedAtMs) >= 0 &&
        static_cast<uint32_t>(receivedAtMs - cupPresence.lastRemovedAtMs) <=
            DIRECT_STOP_CONFIRMATION_WINDOW_MS &&
        (packetSequence == 0 || cupPresence.lastRemovedPacketSequence == 0 ||
         packetSequence == (cupPresence.lastRemovedPacketSequence == UINT32_MAX
                                ? 1U : cupPresence.lastRemovedPacketSequence + 1U));
    cupPresence.removedConfirmations = consecutive
        ? static_cast<uint8_t>(cupPresence.removedConfirmations + 1U)
        : 1U;
    cupPresence.lastRemovedAtMs = receivedAtMs;
    cupPresence.lastRemovedPacketSequence = packetSequence;

    if (cupPresence.removedConfirmations < DIRECT_STOP_CONFIRMATION_SAMPLES) {
      return CupPresenceEvent::NONE;
    }

    cupPresence.state = CupPresenceState::ABSENT;
    cupPresence.taredWhilePresent = false;
    cupPresence.referenceUncertain = false;
    cupPresence.occupiedReferenceG = 0.0f;
    cupPresence.inNegativeHole = true;
    cupPresence.holeWeightG = weight;
    cupPresence.removedConfirmations = 0;
    resetCupPlaceStabilityStreak();
    return CupPresenceEvent::REMOVED;
  }

  if (cupPresence.inNegativeHole && weight < cupPresence.holeWeightG) {
    cupPresence.holeWeightG = weight;
  }
  if (!allowPlacement) {
    resetCupPlaceStabilityStreak();
    return CupPresenceEvent::NONE;
  }

  const bool placeCandidate = weight >= minCupG;
  const bool putBackCandidate =
      cupPresence.inNegativeHole &&
      (weight - cupPresence.holeWeightG) >= minCupG;
  if (!placeCandidate && !putBackCandidate) {
    resetCupPlaceStabilityStreak();
    return CupPresenceEvent::NONE;
  }

  if (cupPresence.placeStabilitySamples == 0) {
    cupPresence.placeCandidateWeightG = weight;
    cupPresence.placeMinimumG = weight;
    cupPresence.placeMaximumG = weight;
    cupPresence.placeStabilitySamples = 1;
    cupPresence.placeStabilityStartedAtMs = receivedAtMs;
    cupPresence.placeLastSampleAtMs = receivedAtMs;
    return CupPresenceEvent::NONE;
  }

  if (static_cast<uint32_t>(receivedAtMs - cupPresence.placeLastSampleAtMs) >
          runtimeConfig.retareStabilityMaxGapMs ||
      fmaxf(weight, cupPresence.placeMaximumG) -
              fminf(weight, cupPresence.placeMinimumG) >
          runtimeConfig.retareStabilityToleranceG) {
    cupPresence.placeCandidateWeightG = weight;
    cupPresence.placeMinimumG = weight;
    cupPresence.placeMaximumG = weight;
    cupPresence.placeStabilitySamples = 1;
    cupPresence.placeStabilityStartedAtMs = receivedAtMs;
    cupPresence.placeLastSampleAtMs = receivedAtMs;
    return CupPresenceEvent::NONE;
  }

  cupPresence.placeCandidateWeightG = weight;
  cupPresence.placeMinimumG = fminf(weight, cupPresence.placeMinimumG);
  cupPresence.placeMaximumG = fmaxf(weight, cupPresence.placeMaximumG);
  cupPresence.placeLastSampleAtMs = receivedAtMs;
  if (cupPresence.placeStabilitySamples < UINT8_MAX) {
    ++cupPresence.placeStabilitySamples;
  }
  const uint32_t stableDurationMs = static_cast<uint32_t>(
      receivedAtMs - cupPresence.placeStabilityStartedAtMs);
  const bool samplesMet = cupPresence.placeStabilitySamples >=
                          runtimeConfig.retareStabilitySamples;
  const bool durationMet =
      runtimeConfig.retareStabilityMinDurationMs == 0U ||
      stableDurationMs >= runtimeConfig.retareStabilityMinDurationMs;
  if (!samplesMet || !durationMet) {
    return CupPresenceEvent::NONE;
  }

  const bool placedByWeight =
      cupPresence.placeCandidateWeightG >= minCupG;
  const bool placedByPutBack = cupPresence.inNegativeHole &&
      (cupPresence.placeCandidateWeightG - cupPresence.holeWeightG) >= minCupG;
  if (!placedByWeight && !placedByPutBack) {
    return CupPresenceEvent::NONE;
  }

  cupPresence.state = CupPresenceState::PRESENT;
  ++cupPresence.placementId;
  if (cupPresence.placementId == 0) ++cupPresence.placementId;
  cupPresence.referenceUncertain = false;
  cupPresence.occupiedReferenceG = cupPresence.placeCandidateWeightG;
  cupPresence.occupiedMinimumG = cupPresence.placeMinimumG;
  cupPresence.occupiedMaximumG = cupPresence.placeMaximumG;
  // Retain the same direct/relative minimum predicate for queued validation.
  cupPresence.occupiedPlacementThresholdG = cupPresence.inNegativeHole
      ? fminf(minCupG, cupPresence.holeWeightG + minCupG) : minCupG;
  // Put-back onto a tared hole reads ~0 g with the cup on the pan.
  cupPresence.taredWhilePresent = placedByPutBack && !placedByWeight;
  cupPresence.inNegativeHole = false;
  cupPresence.holeWeightG = 0.0f;
  cupPresence.removedArmed = true;
  resetCupPlaceStabilityStreak();
  return CupPresenceEvent::PLACED;
}
