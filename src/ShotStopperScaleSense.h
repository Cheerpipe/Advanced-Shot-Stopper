#pragma once

#include "ShotStopperScaleTypes.h"
#include "ShotStopperBbwEwma.h"
#include "ShotStopperShotLogTypes.h"

// =============================================================================
// LAYER: Scale (sense / trajectory / first-drop)
// =============================================================================
// WHAT: Shot weight trajectory and first-drop detector. Predicts against a
//       brew-injected target. Cup presence is ShotStopperCupPresence.h.
//       No GPIO. No heap; trajectory is BSS.
//
// BOUNDARY: Weight stream only. Does not call brew or cup — the stopper
// applies those effects. Must not include or know paddle/momentary/reed or
// machine actuators. Never branch on MachineType or PaddleMode.

void resetShotTrajectory(uint32_t startedAtMs) {
  shot.startMs = startedAtMs;
  shot.expectedEndS = session.config.operationalWallMs / 1000.0f;
  shot.datapoints = 0;
  shot.automaticBrew = false;
  shotCurveSampler.reset(startedAtMs);
}

void calculateExpectedEndTime(float cutTargetG) {
  shot.expectedEndS = bbwEwma::predict(
      shot.timeS, shot.weight, shot.datapoints, cutTargetG,
      session.config.operationalWallMs / 1000.0f);
}

void rejectScaleSample(DebugCode code, float weightG, float referenceG = 0.0f) {
  markShotCurveBreak();
  addDebugEvent(DebugCategory::SCALE, code, weightToCentigrams(weightG),
                weightToCentigrams(referenceG));
}

void armPostTareBaselineWindow() {
  markShotCurveBreak();
  session.awaitingPostTareBaseline = true;
  session.postTareBaselineDeadlineMs =
      millis() + session.config.postTareBaselineGraceMs;
  session.hasWeightAnchor = false;
  session.recoveryConfirmations = 0;
  session.recoveryLastAtMs = 0;
  session.recoveryLastPacketSequence = 0;
  resetWeightTrend();
  if (session.weightControlState == WeightControlState::VALIDATING) {
    setWeightControlState(WeightControlState::ACTIVE);
  }
}

bool expirePostTareBaselineIfNeeded() {
  if (!session.awaitingPostTareBaseline) {
    return false;
  }
  if (static_cast<int32_t>(millis() - session.postTareBaselineDeadlineMs) <
      0) {
    return false;
  }
  session.awaitingPostTareBaseline = false;
  session.retareEffectPending = false;
  scaleProfileNoteEvent(ScaleProfileEvent::TARE_BASELINE_TIMEOUT, millis(),
                        session.ownedConnectionGeneration, 0, NAN,
                        session.appliedTareRequestId,
                        static_cast<uint32_t>(IdleTareReason::EFFECT_UNCONFIRMED));
  addDebugEvent(DebugCategory::SCALE,
                DebugCode::SCALE_POST_TARE_BASELINE_TIMEOUT,
                static_cast<int32_t>(session.id),
                static_cast<int32_t>(session.config.postTareBaselineGraceMs));
  return true;
}

bool acceptWeightIntoTrajectory(float weight, uint32_t receivedAtMs,
                                uint32_t packetSequence,
                                float cutTargetG = 0.0f) {
  if (shot.datapoints > 0 && receivedAtMs == session.lastAcceptedWeightAtMs) {
    // Re-transmitted timestamp: counts as freshness for staleness watchers,
    // but a zero-duration sample must not widen the trajectory fit.
    session.receivedFreshWeightInCycle = true;
    session.lastAcceptedWeightG = weight;
    session.lastAcceptedPacketSequence = packetSequence;
    serialTracef(LogLevel::DEBUG, "%.2fg, t=dup, kept freshness only", weight);
    return true;
  }
  size_t index;
  if (shot.datapoints < MAX_SHOT_DATAPOINTS) {
    index = shot.datapoints++;
  } else {
    memmove(&shot.timeS[0], &shot.timeS[1],
            (MAX_SHOT_DATAPOINTS - 1U) * sizeof(float));
    memmove(&shot.weight[0], &shot.weight[1],
            (MAX_SHOT_DATAPOINTS - 1U) * sizeof(float));
    index = MAX_SHOT_DATAPOINTS - 1U;
  }
  shot.timeS[index] =
      static_cast<uint32_t>(receivedAtMs - shot.startMs) / 1000.0f;
  shot.weight[index] = weight;
  shotCurveSampler.accept(weight, receivedAtMs);
  session.receivedFreshWeightInCycle = true;
  session.hasWeightAnchor = true;
  session.lastAcceptedWeightAtMs = receivedAtMs;
  session.lastAcceptedWeightG = weight;
  session.lastAcceptedPacketSequence = packetSequence;
  calculateExpectedEndTime(cutTargetG);

  serialTracef(LogLevel::DEBUG, "%.2fg, t=%.2fs, expected end=%.2fs",
               weight, shot.timeS[index], shot.expectedEndS);
  return true;
}

FirstFlowObservation considerScaleFlowMarkers(float weight,
                                               uint32_t receivedAtMs,
                                               uint32_t packetSequence) {
  if (!session.active || !session.startedWithScale || session.firstDropMs != 0) {
    return {};
  }
  if (!session.scaleBaselineReady) {
    if (fabsf(weight) > FIRST_DROP_BASELINE_SETTLE_G) {
      return {};
    }
    session.scaleBaselineG = weight;
    session.scaleBaselineReady = true;
    return {};
  }
  if (session.firstFlow.phase == FirstFlowPhase::SEEKING &&
      session.firstFlow.confirmations == 0 &&
      fabsf(weight) <= FIRST_DROP_BASELINE_LOCK_G &&
      fabsf(weight) <= fabsf(session.scaleBaselineG)) {
    session.scaleBaselineG = weight;
  }

  const bool seekingStreak = session.firstFlow.phase == FirstFlowPhase::SEEKING &&
      session.firstFlow.confirmations > 0;
  const FirstFlowClass classified =
      stepFirstFlow(session.firstFlow, weight, receivedAtMs, packetSequence,
                    session.scaleBaselineG, runtimeConfig.minimumCupWeightG);
  if (seekingStreak && classified == FirstFlowClass::NONE) {
    // A confirmation streak died sub-threshold without firing: without this
    // telemetry a mistuned threshold is indistinguishable from a dead sensor.
    scaleProfileNoteEvent(ScaleProfileEvent::FIRST_DROP_SEEKING_FA, millis(),
                          session.ownedConnectionGeneration, packetSequence,
                          weight, session.scaleBaselineG, 0);
  }
  if (classified == FirstFlowClass::FIRE) {
    return {session.firstFlow.candidateMs != 0 ? session.firstFlow.candidateMs
                                               : receivedAtMs,
            session.firstFlow.candidateMs != 0
                ? session.firstFlow.candidateWeightG
                : weight};
  }
  return {};
}
