// Scale timer session
// ---------------------------------------------------------------------------

void requestRemoteTimerStop();
void flushPendingScaleTimerStopNow();
void scheduleScaleTimerStopAfterCycle(uint32_t internalElapsedMs);
void servicePendingScaleTimerStop();

bool requestRemoteTimerStart() {
  ScaleCommand command;
  command.type = ScaleCommandType::START_TIMER_AND_TARE;
  command.cycleId = session.id;
  command.autoTare = session.config.autoTare;
  command.canTareStartTimer = session.config.canTareStartTimer;
  command.commandFeedbackExpected =
      getScaleLinkSnapshot().features.has(ScaleFeatureCommandAudibleFeedback);
  session.timerStartCommandQueued = enqueueScaleCommand(command);
  session.remoteTimerMayBeRunning = session.timerStartCommandQueued;
  return session.timerStartCommandQueued;
}

bool requestRemoteRetare() {
  if (!session.config.autoTare) {
    return false;
  }
  ScaleCommand command;
  command.type = ScaleCommandType::TARE_ONLY;
  command.cycleId = session.id;
  command.autoTare = true;
  command.commandFeedbackExpected =
      getScaleLinkSnapshot().features.has(ScaleFeatureCommandAudibleFeedback);
  return enqueueScaleCommand(command);
}

bool tryQueueRemoteTimerStop() {
  if (!session.stopTimerRequested || session.stopTimerCommandQueued ||
      session.timerStopResult == TimerStopResult::WRITE_SUCCEEDED ||
      session.stopTimerAttempts >= SCALE_STOP_MAX_ATTEMPTS ||
      static_cast<int32_t>(millis() - session.stopTimerRetryDeadlineMs) >= 0 ||
      !scaleAvailable()) {
    return false;
  }
  ScaleCommand command;
  command.type = ScaleCommandType::STOP_TIMER;
  command.cycleId = session.id;
  command.commandFeedbackExpected =
      getScaleLinkSnapshot().features.has(ScaleFeatureCommandAudibleFeedback);
  ++session.stopTimerAttempts;
  session.stopTimerLastAttemptMs = millis();
  if (!enqueueScaleCommand(command, session.remoteTimerStartSettled)) {
    session.timerStopResult = TimerStopResult::NOT_ATTEMPTED;
    return false;
  } else {
    session.timerStopResult = TimerStopResult::PENDING;
    session.stopTimerCommandQueued = true;
    serialTracef(LogLevel::DEBUG, "Remote timer stop queued for cycle %lu",
                 static_cast<unsigned long>(session.id));
    return true;
  }
}

void requestRemoteTimerStop() {
  if (!session.timerStartCommandQueued || session.stopTimerRequested) {
    return;
  }

  session.stopTimerRequested = true;
  session.stopTimerRetryDeadlineMs = millis() + SCALE_STOP_RETRY_WINDOW_MS;
  if (!tryQueueRemoteTimerStop()) {
    session.timerStopResult = TimerStopResult::NOT_ATTEMPTED;
    serialTracef(LogLevel::DEBUG, "Remote timer stop deferred for cycle %lu",
                 static_cast<unsigned long>(session.id));
  }
}

void serviceRemoteTimerStopRetry() {
  if (!session.stopTimerRequested || session.stopTimerCommandQueued ||
      session.timerStopResult == TimerStopResult::WRITE_SUCCEEDED ||
      session.stopTimerAttempts >= SCALE_STOP_MAX_ATTEMPTS ||
      static_cast<int32_t>(millis() - session.stopTimerRetryDeadlineMs) >= 0) {
    return;
  }
  if (session.stopTimerAttempts > 0 &&
      elapsedMs(session.stopTimerLastAttemptMs) <
          SCALE_STOP_RETRY_INTERVAL_MS) {
    return;
  }
  (void)tryQueueRemoteTimerStop();
}
// ---------------------------------------------------------------------------
// Brew policy + scale sense + cup (domain headers). Orchestrator glue follows.
// These layers stay decoupled: no paddle/momentary internals, no direct calls
// between brew ↔ cup ↔ scale — this file applies their effects.
// ---------------------------------------------------------------------------

void notifyWebhookFirstDrop(uint32_t receivedAtMs);

#include "ShotStopperCupPresence.h"
#include "ShotStopperBrew.h"
#include "ShotStopperRinse.h"
#include "ShotStopperScaleSense.h"

// Placement policy and request provenance stay on the control task. The cup
// detector still owns all presence/stability decisions; the worker owns ATT.
struct IdleTareRuntime {
  uint32_t connectionGeneration = 0;
  uint32_t configRevision = 0;
  uint32_t lastSampleAtMs = 0;
  uint32_t lastPacketSequence = 0;
  uint32_t requestId = 0;
  uint32_t requestedAtMs = 0;
  bool eligible = false;
  bool absentObserved = false;
  bool cupStillPresent = false;
  bool tareNotified = false;
  bool previouslyTared = false;
  bool startNeedsRelease = false;
};
IdleTareRuntime idleTare;
uint32_t nextIdleTareRequestId = 1;

void cancelPendingFinalize(const char *reason);

bool idleTareEligible() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  const bool endedAwaitingRelease =
      stopperState == StopperState::REQUIRES_OFF && lastCycle.valid &&
      lastCycle.cycleId == session.id &&
      !brewEndIsAbandonedStart(lastCycle.endReason) &&
      lastCycle.endReason != EndReason::RINSE_COMPLETE &&
      lastCycle.endReason != EndReason::GLOBAL_LIMIT &&
      lastCycle.endReason != EndReason::CONFIGURED_WALL_LIMIT &&
      lastCycle.endReason != EndReason::RELAY_SAFETY_FAILURE;
  return runtimeConfig.autoTareOutsideBrew && firmwareInitializationComplete &&
         !session.active && !maintenanceLease.active && !relay.closed &&
         !relay.tripped && !relay.operationalTripped &&
         (stopperState == StopperState::READY || endedAwaitingRelease) &&
         machineRunState() == MachineRunState::CONFIRMED_OFF &&
         scaleLinkAvailable(getScaleLinkSnapshot());
}

void releaseIdleTareRequest() {
  IdleTareStatus released;
  if (!cancelIdleScaleTare(idleTare.requestId, &released)) return;
  // Preserve the actual outcome even when config changes race with completion.
  // The worker returns terminal status atomically with releasing the request.
  if (idleTare.cupStillPresent && !idleTare.tareNotified &&
      released.phase == IdleTarePhase::SUCCEEDED) {
    notifyCupPresenceTare();
  } else if (idleTare.cupStillPresent && idleTare.tareNotified &&
             released.phase == IdleTarePhase::FAILED) {
    restoreCupTareReference(idleTare.previouslyTared);
  }
  idleTare.requestId = 0;
  idleTare.tareNotified = false;
  idleTare.cupStillPresent = false;
}

void serviceIdleTare() {
  if (!runtimeConfig.autoTareOutsideBrew && idleTare.requestId == 0 &&
      !idleTare.eligible) return;
  const uint32_t generation = getScaleLinkSnapshot().connectionGeneration;
  const bool eligible = idleTareEligible();
  if (eligible != idleTare.eligible ||
      generation != idleTare.connectionGeneration ||
      runtimeConfig.revision != idleTare.configRevision) {
    idleTare.absentObserved = false;
    resetCupPlaceStabilityStreak();
    if (generation != idleTare.connectionGeneration) {
      idleTare.lastSampleAtMs = 0;
      idleTare.lastPacketSequence = 0;
      idleTare.cupStillPresent = false;
    }
    idleTare.eligible = eligible;
    idleTare.connectionGeneration = generation;
    idleTare.configRevision = runtimeConfig.revision;
    // Revoke queued requests; executing writes retain the start barrier.
    if (idleTare.requestId != 0) {
      releaseIdleTareRequest();
    }
  }
  if (idleTare.requestId == 0) return;
  if (cupPresenceState() != CupPresenceState::PRESENT) {
    idleTare.cupStillPresent = false;
  }
  const IdleTareStatus status = idleScaleTareStatus();
  const bool failed = status.requestId != idleTare.requestId ||
                      status.phase == IdleTarePhase::NONE ||
                      status.phase == IdleTarePhase::FAILED;
  if (failed && idleTare.tareNotified && idleTare.cupStillPresent) {
    restoreCupTareReference(idleTare.previouslyTared);
  }
  if (!failed && status.phase == IdleTarePhase::SUCCEEDED &&
      idleTare.cupStillPresent && !idleTare.tareNotified) {
    notifyCupPresenceTare();
    idleTare.tareNotified = true;
  }
  if (failed || !idleTare.cupStillPresent ||
      elapsedMs(idleTare.requestedAtMs) >=
          SCALE_ATT_TIMEOUT_MS + runtimeConfig.postTareBaselineGraceMs) {
    releaseIdleTareRequest();
  }
}

bool idleTareBlocksStart() {
  serviceIdleTare();
  if (idleTare.requestId != 0) {
    idleTare.startNeedsRelease = true;
    // A queued tare can be revoked, but this start gesture stays rejected.
    const IdleTareStatus status = idleScaleTareStatus();
    if (status.phase == IdleTarePhase::QUEUED) releaseIdleTareRequest();
    return true;
  }
  return idleTare.startNeedsRelease;
}

bool observeIdleTareSample(float weight, uint32_t atMs, uint32_t sequence,
                           uint32_t generation) {
  serviceIdleTare();
  if (generation != idleTare.connectionGeneration ||
      static_cast<int32_t>(millis() - atMs) >
          static_cast<int32_t>(MAX_AUTOMATION_WEIGHT_AGE_MS) ||
      (idleTare.lastPacketSequence != 0 &&
       (static_cast<int32_t>(sequence - idleTare.lastPacketSequence) <= 0 ||
        static_cast<int32_t>(atMs - idleTare.lastSampleAtMs) < 0))) {
    resetCupPlaceStabilityStreak();
    return false;
  }
  idleTare.lastSampleAtMs = atMs;
  idleTare.lastPacketSequence = sequence;
  if (idleTare.requestId != 0 && idleTare.cupStillPresent) {
    const IdleTareStatus status = idleScaleTareStatus();
    if (status.requestId == idleTare.requestId &&
        (status.phase == IdleTarePhase::WRITING ||
         status.phase == IdleTarePhase::SUCCEEDED)) {
      if (!idleTare.tareNotified) {
        notifyCupPresenceTare();
        idleTare.tareNotified = true;
      }
      if (status.phase == IdleTarePhase::SUCCEEDED &&
          static_cast<int32_t>(atMs - status.writtenAtMs) > 0 &&
          fabsf(weight) <= FIRST_DROP_BASELINE_SETTLE_G) {
        releaseIdleTareRequest();
      }
    }
  }
  return true;
}

void handleIdleCupEvent(CupPresenceEvent event, float weight) {
  if (event == CupPresenceEvent::REMOVED) {
    idleTare.cupStillPresent = false;
    if (idleTare.requestId != 0) releaseIdleTareRequest();
    if (!session.active && pendingFinalize.pending) {
      machineCancelSettledWeightCutOff();
      cancelPendingFinalize("Cup removed during drip analysis");
    }
  }
  if (!idleTare.eligible) return;
  if (cupPresenceState() == CupPresenceState::ABSENT &&
      weight < runtimeConfig.minimumCupWeightG) {
    idleTare.absentObserved = true;
  }
  if (event != CupPresenceEvent::PLACED) return;
  const bool placedInIdle = idleTare.absentObserved;
  idleTare.absentObserved = false;
  if (!placedInIdle || idleTare.requestId != 0 ||
      !getScaleLinkSnapshot().features.has(ScaleFeatureTare)) return;
  if (pendingFinalize.pending) {
    machineCancelSettledWeightCutOff();
    cancelPendingFinalize("New cup placed during drip analysis");
  }
  ScaleCommand command;
  command.type = ScaleCommandType::TARE_ONLY;
  command.autoTare = true;
  command.idleTareRequestId = nextIdleTareRequestId++;
  if (nextIdleTareRequestId == 0) nextIdleTareRequestId = 1;
  command.connectionGeneration = idleTare.connectionGeneration;
  command.expiresAtMs = millis() + MAX_AUTOMATION_WEIGHT_AGE_MS;
  command.commandFeedbackExpected =
      getScaleLinkSnapshot().features.has(ScaleFeatureCommandAudibleFeedback);
  if (!enqueueScaleCommand(command)) return;
  idleTare.requestId = command.idleTareRequestId;
  idleTare.requestedAtMs = millis();
  idleTare.cupStillPresent = true;
  idleTare.previouslyTared = cupPresenceIsTared();
  idleTare.tareNotified = false;
  emitImmediateCommandAlertIfBuzzer(AlertEvent::TARE);
}

#ifndef SHOT_STOPPER_HOST_TEST
uint32_t webhookUnixSecAt(uint32_t occurredAtMs) {
  if (!g_wallClock.synced()) return 0U;
  const uint32_t nowMs = millis();
  const uint32_t nowUtcSec = g_wallClock.nowUtcSec(nowMs);
  const uint32_t ageSec = static_cast<uint32_t>(nowMs - occurredAtMs) / 1000U;
  return nowUtcSec >= ageSec ? nowUtcSec - ageSec : 0U;
}

WebhookEvent baseWebhookEvent(WebhookEventType type, uint32_t cycleId,
                              uint32_t occurredAtMs = millis()) {
  WebhookEvent event;
  event.type = type;
  event.cycleId = cycleId;
  event.uptimeMs = occurredAtMs;
  event.unixSec = webhookUnixSecAt(occurredAtMs);
  return event;
}

void queueWebhookBrewStart() {
  if (!webhookBrewStartPending) return;
  webhookBrewStartPending = false;
  const WebhookConfig config = networkManager.webhookConfig();
  if (!config.enabled || !config.brewState) return;
  const uint32_t startedAtMs = session.circuitClosedAtMs != 0U
                                   ? session.circuitClosedAtMs
                                   : session.startedAtMs;
  WebhookEvent event = baseWebhookEvent(WebhookEventType::BREWING, session.id,
                                        startedAtMs);
  event.targetWeightG = static_cast<float>(session.config.goalWeightG);
  event.presetId = session.activePresetId;
  (void)networkManager.enqueueWebhook(event);
}

void serviceWebhookBrewStart() {
  if (!webhookBrewStartPending || !session.active) return;
  const uint32_t ageMs = elapsedMs(session.startedAtMs);
  if ((ageMs >= WEBHOOK_START_MIN_DELAY_MS && currentWeightIsFresh() &&
       currentWeightSequence != session.weightSequenceAtStart) ||
      ageMs >= WEBHOOK_START_MAX_DELAY_MS) {
    queueWebhookBrewStart();
  }
}

void notifyWebhookFirstDrop(uint32_t receivedAtMs) {
  queueWebhookBrewStart();
  const WebhookConfig config = networkManager.webhookConfig();
  if (!config.enabled || !config.firstDrop) return;
  WebhookEvent event = baseWebhookEvent(WebhookEventType::FIRST_DROP, session.id,
                                        receivedAtMs);
  const uint32_t anchor = session.circuitClosedAtMs != 0
                              ? session.circuitClosedAtMs
                              : session.startedAtMs;
  event.firstDropValid = static_cast<int32_t>(receivedAtMs - anchor) >= 0;
  event.firstDropMs = event.firstDropValid ? receivedAtMs - anchor : 0;
  event.weightValid = isfinite(currentWeight);
  event.weightG = event.weightValid ? currentWeight : 0.0f;
  event.targetWeightG = static_cast<float>(session.config.goalWeightG);
  event.presetId = session.activePresetId;
  (void)networkManager.enqueueWebhook(event);
}
#else
void notifyWebhookFirstDrop(uint32_t receivedAtMs) { (void)receivedAtMs; }
void serviceWebhookBrewStart() {}
#endif

#if defined(SHOT_STOPPER_HOST_TEST)
#define SHOT_STOPPER_SCALE_WORKER_IN_ORCHESTRATOR
#include "ShotStopperScaleWorker.cpp"
#endif

void copyDebugExportExtras(DebugExportExtras &out,
                           const ControlStatusSnapshot &control) {
  out = DebugExportExtras{};
  if (session.active) {
    out.sessionActive = true;
    out.sessionAutomaticEnabled = session.automaticEnabled;
    out.sessionBbwProtectionEnabled = session.bbwProtectionEnabled;
    out.sessionBbwProtectionEnded = session.bbwProtectionEnded;
    out.sessionStartedWithScale = session.startedWithScale;
    out.sessionScaleWasLost = session.scaleWasLost;
    out.sessionCupRemovedPending = session.cupRemovedPending;
    out.sessionExtractionExtended = session.extractionExtended;
    out.sessionSlowExtractionExtended = session.slowExtractionExtended;
    out.sessionTargetReachedEarly = session.targetReachedEarly;
    out.sessionAutoToManualGuardArmed = session.autoToManualGuardArmed;
    out.sessionAutoToManualGuardEnforced = session.autoToManualGuardEnforced;
    out.sessionAccidentalTouchHolding = session.accidentalTouchHolding;
    out.sessionAccidentalTouchPhase =
        static_cast<uint8_t>(session.accidentalTouchPhase);
    out.sessionAccidentalTouchClass =
        static_cast<uint8_t>(session.accidentalTouchClass);
    out.sessionAccidentalTouchPendingCount = session.accidentalTouchPendingCount;
    out.sessionActivePresetId = session.activePresetId;
    out.sessionWeightControlState =
        static_cast<uint8_t>(session.weightControlState);
    out.sessionId = session.id;
    out.sessionStartedAtMs = session.startedAtMs;
    out.sessionFirstDropMs = session.firstDropMs;
    out.sessionAutoToManualGuardDeadlineAtMs =
        session.autoToManualGuardDeadlineAtMs;
    out.sessionTargetReachedAtMs = session.targetReachedAtMs;
  }
  {
    CupPresenceState cupState = CupPresenceState::ABSENT;
    copyCupPresenceDebug(
        cupState, out.cupHoldTransitions, out.cupInNegativeHole,
        out.cupRemovedArmed, out.cupRemovedConfirmations,
        out.cupPlaceStabilitySamples, out.cupHoleWeightG,
        out.cupPlaceCandidateWeightG);
    out.cupState = static_cast<uint8_t>(cupState);
  }
  {
    const ScaleLinkSnapshot link = getScaleLinkSnapshot();
    out.scaleLinkState = static_cast<uint8_t>(link.state);
    out.scaleDisconnectSequence = link.disconnectSequence;
    out.scaleConnectionGeneration = link.connectionGeneration;
    out.scalePacketSequence = link.packetSequence;
    out.scaleWorkerProgressAtMs = link.workerProgressAtMs;
    out.scaleTimerValid = link.timerValid;
    out.scaleTimerMs = link.timerMs;
    out.scaleTimerAgeMs = link.timerAgeMs;
    copyCString(out.scaleProtocolName, sizeof(out.scaleProtocolName),
                link.protocolName);
  }
  out.relay = getRelaySafetySnapshot();
  out.rawActivatorOn = control.rawActivatorOn;
  out.physicalActivatorOn = control.physicalActivatorOn;
  out.machineRunState = static_cast<uint8_t>(control.machineRunState);
  out.machineStartAckPending = control.machineStartAckPending;
  out.machineStopAckPending = control.machineStopAckPending;
  out.machineOrphanRun = control.machineOrphanRun;
  out.noScaleShotGuardHold = control.noScaleShotGuardHold;
  out.noScaleShotGuardScaleWasAvailable =
      control.noScaleShotGuardScaleWasAvailable;
  out.cupStartGuardHold = control.cupStartGuardHold;
  out.healthHeapAlertLatched = healthHeapAlertLatched;
  out.healthStackAlertLatched = healthStackAlertLatched;
  out.healthLoopGapAlertLatched = healthLoopGapAlertLatched;
}

GuardInputs loopGuardInputs;

void observeMachineSenseFromSession() {
  MachineSense sense;
  if (session.active && session.hasWeightAnchor &&
      isfinite(session.lastAcceptedWeightG)) {
    sense.weightG = session.lastAcceptedWeightG;
    sense.weightSequence = session.lastAcceptedPacketSequence != 0
                               ? session.lastAcceptedPacketSequence
                               : currentWeightSequence;
  } else {
    sense.weightG = currentWeight;
    sense.weightSequence = currentWeightSequence;
  }
  sense.weightFresh = observedWeightIsFresh();
  sense.accidentalHold = session.accidentalTouchHolding;
  sense.brewCycleActive = session.active;
  sense.firstDropSeen = session.firstDropMs != 0;
  sense.scaleConnectedEdge = scaleWorkerTakeConnectedEdge();
  sense.cupRemovedEdge = pendingCupRemovedSettle;
  pendingCupRemovedSettle = false;
  machineObserveSense(sense);
}

void fillLoopGuardsFromIntention(const MachineIntention &intention) {
  loopGuardInputs.holdActive = intention.holdActive;
  loopGuardInputs.physicalOn = intention.physicalOn;
  loopGuardInputs.stablyOff = intention.stablyOff;
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  loopGuardInputs.scaleAvailable = scaleLinkAvailable(link);
  loopGuardInputs.scaleUsable =
      loopGuardInputs.scaleAvailable && currentWeightIsFresh();
  loopGuardInputs.cup = cupPresenceState();
  loopGuardInputs.currentWeightG = currentWeight;
  // Product reuses the machine rinse-gesture window as the blocked-start hold
  // timeout. Brew sees only this composed field, not rinseGestureMs.
  loopGuardInputs.blockedHoldTimeoutMs = runtimeConfig.rinseGestureMs;
}

void captureLoopGuards() {
  fillLoopGuardsFromIntention(machinePollIntention());
}

void refreshLoopGuardsFromLastIntention() {
  fillLoopGuardsFromIntention(machineLastIntention());
}

void pushActivatorDrivePermission() {
  serviceIdleTare();
  if (!machineLastIntention().holdActive &&
      !machineLastIntention().physicalOn) {
    idleTare.startNeedsRelease = false;
  }
  // Remember the permission sampled by the activator for this whole pass,
  // even if a zero sample completes tare before beginCycle later in the pass.
  if (idleTare.requestId != 0) idleTare.startNeedsRelease = true;
  refreshLoopGuardsFromLastIntention();
  machineSetActivatorDriveAllowed(
      session.active || (idleTare.requestId == 0 &&
                         !idleTare.startNeedsRelease &&
                         !guardsWouldBlockActivatorDrive(loopGuardInputs)));
}

void orchestratePostTareBaselineArm() {
  armPostTareBaselineWindow();
  notifyCupPresenceTare();
  holdCupPresenceTransitions(true);
}

bool acceptWeightAndNotifyFirstFlow(float weight, uint32_t receivedAtMs,
                                    uint32_t packetSequence) {
  if (!acceptWeightIntoTrajectory(weight, receivedAtMs, packetSequence,
                                  activeWeightCutTargetG())) {
    return false;
  }
  const uint32_t latchAtMs =
      maybeLatchFirstFlowFromAcceptedWeight(weight, receivedAtMs);
  if (latchAtMs != 0) {
    onFirstDropsDetected(latchAtMs);
  }
  return true;
}

bool recordWeightSampleWithProvenance(float weight, uint32_t receivedAtMs,
                                      uint32_t packetSequence,
                                      uint32_t connectionGeneration) {
  if (!isfinite(weight) || fabsf(weight) > MAX_PARSED_WEIGHT_G) {
    serialTrace(LogLevel::WARNING, "Invalid or out-of-range weight ignored");
    rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_INVALID, weight);
    return false;
  }

  if (connectionGeneration == 0) {
    connectionGeneration = session.ownedConnectionGeneration;
  }
  if (packetSequence == 0) {
    packetSequence = session.lastAcceptedPacketSequence + 1U;
    if (packetSequence == 0) {
      packetSequence = 1;
    }
  }

  if (runtimeConfig.autoTareOutsideBrew && !session.active &&
      (weight < MIN_AUTOMATION_WEIGHT_G || weight > MAX_AUTOMATION_WEIGHT_G)) {
    resetCupPlaceStabilityStreak();
    return false;
  }
  if ((runtimeConfig.autoTareOutsideBrew || idleTare.requestId != 0) &&
      !session.active &&
      !observeIdleTareSample(weight, receivedAtMs, packetSequence,
                             connectionGeneration)) return false;
  const CupPresenceEvent cupEvent =
      feedCupPresence(weight, receivedAtMs, packetSequence);
  if ((runtimeConfig.autoTareOutsideBrew || idleTare.requestId != 0) &&
      !session.active && weight >= MIN_AUTOMATION_WEIGHT_G &&
      weight <= MAX_AUTOMATION_WEIGHT_G) {
    handleIdleCupEvent(cupEvent, weight);
  }
  if (cupEvent == CupPresenceEvent::REMOVED) {
    // Run-state settle for momentary-only ASSUMED_OFF — independent of cut.
    pendingCupRemovedSettle = true;
    if (session.active && stopperState == StopperState::BREW &&
        !session.config.timerOnly && session.config.cupProtectionEnabled &&
        session.config.stopIfCupRemoved && session.startedWithScale) {
      session.cupRemovedPending = true;
      addDebugEvent(DebugCategory::SCALE, DebugCode::CUP_REMOVED_CONFIRMED,
                    weightToCentigrams(weight),
                    static_cast<int32_t>(elapsedMs(session.startedAtMs)));
    }
  }
  if (cupEvent == CupPresenceEvent::PLACED && session.active &&
      session.startedWithScale && retareWindowOpen() &&
      !session.awaitingPostTareBaseline && !session.flowDuringRetare &&
      session.firstDropMs == 0 && weight >= MIN_AUTOMATION_WEIGHT_G &&
      weight <= MAX_AUTOMATION_WEIGHT_G) {
    performAutomaticRetare();
  }

  if (session.active && session.startedWithScale) {
    const uint32_t firstDropAtMs =
        considerScaleFlowMarkers(weight, receivedAtMs, packetSequence);
    if (firstDropAtMs != 0) {
      onFirstDropsDetected(firstDropAtMs);
    }
  }

  if (!shouldTrackWeight()) {
    return weight >= MIN_AUTOMATION_WEIGHT_G &&
           weight <= MAX_AUTOMATION_WEIGHT_G;
  }

  if ((session.weightControlState == WeightControlState::ACTIVE ||
       session.weightControlState == WeightControlState::VALIDATING) &&
      session.ownedConnectionGeneration != 0 &&
      connectionGeneration != session.ownedConnectionGeneration) {
    suspendWeightControl();
  }

  if (weight < MIN_AUTOMATION_WEIGHT_G ||
      weight > MAX_AUTOMATION_WEIGHT_G) {
    if (fabsf(weight) > MAX_AUTOMATION_WEIGHT_G) {
      considerDirectStopSample(weight, receivedAtMs, packetSequence,
                               connectionGeneration);
    }
    weightStreamState = WeightStreamState::OVERLOAD;
    session.calibrationEligible = false;
    if (session.weightControlState == WeightControlState::ACTIVE) {
      setWeightControlState(WeightControlState::VALIDATING);
    }
    session.recoveryConfirmations = 0;
    const float violatedLimit = weight < MIN_AUTOMATION_WEIGHT_G
                                    ? MIN_AUTOMATION_WEIGHT_G
                                    : MAX_AUTOMATION_WEIGHT_G;
    rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_RANGE, weight,
                      violatedLimit);
    return false;
  }

  if (static_cast<int32_t>(receivedAtMs - shot.startMs) < 0) {
    rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_PRE_CYCLE, weight);
    return false;
  }

  if (session.awaitingPostTareBaseline) {
    if (fabsf(weight) <= POST_TARE_BASELINE_MAX_ABS_G) {
      session.awaitingPostTareBaseline = false;
      holdCupPresenceTransitions(false);
      weightStreamState = WeightStreamState::FRESH;
      return acceptWeightAndNotifyFirstFlow(weight, receivedAtMs,
                                            packetSequence);
    }
    rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_SLEW, weight,
                      POST_TARE_BASELINE_MAX_ABS_G);
    return false;
  }

  bool plausible = true;
  if (session.hasWeightAnchor) {
    if (static_cast<int32_t>(receivedAtMs -
                             session.lastAcceptedWeightAtMs) < 0) {
      plausible = false;
    } else {
      uint32_t deltaMs = receivedAtMs - session.lastAcceptedWeightAtMs;
      if (deltaMs == 0) {
        deltaMs = 1;
      }
      const float allowedDelta = AUTOMATION_WEIGHT_SLEW_ALLOWANCE_G +
          MAX_AUTOMATION_WEIGHT_SLEW_G_PER_S * deltaMs / 1000.0f;
      plausible = fabsf(weight - session.lastAcceptedWeightG) <= allowedDelta;
    }
  }

  if (session.weightControlState == WeightControlState::ACTIVE && !plausible) {
    weightStreamState = WeightStreamState::ANOMALOUS;
    session.calibrationEligible = false;
    setWeightControlState(WeightControlState::VALIDATING);
    session.recoveryConfirmations = 0;
    session.recoveryLastAtMs = 0;
    session.recoveryLastPacketSequence = 0;
    rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_SLEW, weight,
                      session.lastAcceptedWeightG);
    return false;
  }

  if (session.weightControlState == WeightControlState::VALIDATING ||
      session.weightControlState == WeightControlState::SUSPENDED) {
    bool recoveryPlausible = plausible;
    if (session.recoveryConfirmations > 0) {
      const bool consecutive =
          packetSequence == session.recoveryLastPacketSequence + 1U;
      const int32_t signedDelta =
          static_cast<int32_t>(receivedAtMs - session.recoveryLastAtMs);
      if (!consecutive || signedDelta <= 0 ||
          static_cast<uint32_t>(signedDelta) > MAX_AUTOMATION_WEIGHT_AGE_MS) {
        recoveryPlausible = false;
      } else {
        const float allowedIncrease = AUTOMATION_WEIGHT_SLEW_ALLOWANCE_G +
            MAX_AUTOMATION_WEIGHT_SLEW_G_PER_S *
                static_cast<uint32_t>(signedDelta) / 1000.0f;
        const float change = weight - session.recoveryLastWeightG;
        recoveryPlausible = change >= -MAX_RECOVERY_WEIGHT_DROP_G &&
                            change <= allowedIncrease;
      }
    } else if (session.hasWeightAnchor) {
      recoveryPlausible = recoveryPlausible &&
          weight >= session.lastAcceptedWeightG - MAX_RECOVERY_WEIGHT_DROP_G;
    }

    if (!recoveryPlausible) {
      const float referenceG = session.recoveryConfirmations > 0
                                   ? session.recoveryLastWeightG
                                   : session.lastAcceptedWeightG;
      session.recoveryConfirmations = 0;
      session.recoveryLastAtMs = 0;
      session.recoveryLastPacketSequence = 0;
      rejectScaleSample(DebugCode::SCALE_SAMPLE_REJECTED_RECOVERY, weight,
                        referenceG);
      return false;
    }

    ++session.recoveryConfirmations;
    session.recoveryLastAtMs = receivedAtMs;
    session.recoveryLastPacketSequence = packetSequence;
    session.recoveryLastWeightG = weight;
    if (session.recoveryConfirmations <
        WEIGHT_RECOVERY_CONFIRMATION_SAMPLES) {
      return false;
    }

    session.ownedConnectionGeneration = connectionGeneration;
    session.scaleDisconnectSequenceAtStart =
        getScaleLinkSnapshot().disconnectSequence;
    session.recoveryConfirmations = 0;
    resetWeightTrend();
    session.hasWeightAnchor = true;
    session.lastAcceptedWeightG = weight;
    session.lastAcceptedWeightAtMs = receivedAtMs;
    session.lastAcceptedPacketSequence = packetSequence;
    setWeightControlState(WeightControlState::ACTIVE);
  }

  if (accidentalTouchSessionActive()) {
    const AccidentalTouchClass classified =
        evaluateAccidentalTouchSample(weight, receivedAtMs);
    if (classified == AccidentalTouchClass::TOUCH) {
      resetDirectStopConfirmation();
      weightStreamState = WeightStreamState::FRESH;
      return true;
    }
  }

  considerDirectStopSample(weight, receivedAtMs, packetSequence,
                           connectionGeneration);
  weightStreamState = WeightStreamState::FRESH;
  return acceptWeightAndNotifyFirstFlow(weight, receivedAtMs, packetSequence);
}

// Host-test-only entry point: the firmware path feeds weight through
// processScaleWorkerEvents() -> recordWeightSampleWithProvenance(). Kept out
// of the ESP32 build so a future caller cannot bypass the worker's sequence
// accounting from a BLE callback.
#if defined(SHOT_STOPPER_HOST_TEST)
bool recordWeightSample(float weight, uint32_t receivedAtMs) {
  uint32_t packetSequence = 0;
  portENTER_CRITICAL(&scaleLinkMux);
  ++scalePacketSequence;
  if (scalePacketSequence == 0) {
    scalePacketSequence = 1;
  }
  packetSequence = scalePacketSequence;
  portEXIT_CRITICAL(&scaleLinkMux);
  return recordWeightSampleWithProvenance(
      weight, receivedAtMs, packetSequence,
      session.ownedConnectionGeneration);
}
#endif


bool endingRinseCycle(EndReason reason) {
  return stopperState == StopperState::RINSE ||
         reason == EndReason::RINSE_COMPLETE;
}

bool pendingFinalizeIsRinse(const PendingShotFinalize &snapshot) {
  return snapshot.endReason == EndReason::RINSE_COMPLETE ||
         snapshot.finalState == StopperState::RINSE;
}

void schedulePendingShotFinalize(EndReason reason, uint32_t durationMs) {
  if (brewEndIsAbandonedStart(reason) || endingRinseCycle(reason)) {
    return;
  }
  const bool logEligible = shotLogEligible(reason, durationMs);
  const bool bbwEligible =
      shotLogBbwEligible(session.startedWithScale, session.config.timerOnly,
                         shot.automaticBrew);
  const bool offsetAnalysis = shot.automaticBrew && !session.config.timerOnly &&
                              session.calibrationEligible &&
                              !session.extractionExtended &&
                              !session.slowExtractionExtended;
  bool webhookEligible = false;
#ifndef SHOT_STOPPER_HOST_TEST
  const WebhookConfig webhook = networkManager.webhookConfig();
  webhookEligible = webhook.enabled && webhook.end;
#endif
  if (!(logEligible && bbwEligible) && !offsetAnalysis && !webhookEligible) {
    return;
  }

  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.offsetAnalysis = offsetAnalysis;
  pendingFinalize.logEligible = logEligible;
  pendingFinalize.endedAtMs = millis();
  pendingFinalize.dripDelayMs = session.config.dripDelayMs;
  pendingFinalize.endedWeightSequence = currentWeightSequence;
  pendingFinalize.cycleStartedAtMs =
      session.circuitClosedAtMs != 0U ? session.circuitClosedAtMs : session.startedAtMs;
  pendingFinalize.bootId = shotLog.bootId();
  pendingFinalize.durationDs =
      static_cast<uint16_t>(durationMs / 100U);
  const uint32_t shotAnchorMs = pendingFinalize.cycleStartedAtMs;
  if (session.firstDropMs != 0 &&
      static_cast<int32_t>(session.firstDropMs - shotAnchorMs) >= 0) {
    pendingFinalize.firstDropDs = static_cast<uint16_t>(
        (session.firstDropMs - shotAnchorMs) / 100U);
  }
  pendingFinalize.goalWeightG = session.config.goalWeightG;
  pendingFinalize.weightOffsetG = session.config.weightOffsetG;
  pendingFinalize.scaleBaselineG =
      session.scaleBaselineReady ? session.scaleBaselineG : 0.0f;
  pendingFinalize.startedWithScale = session.startedWithScale;
  pendingFinalize.timerOnly = session.config.timerOnly;
  pendingFinalize.automaticBrew = shot.automaticBrew;
  pendingFinalize.finalState = stopperState;
  pendingFinalize.endReason = reason;
  pendingFinalize.extractionGuardEnabled =
      session.config.fastExtractionGuardEnabled;
  pendingFinalize.extractionExtended = session.extractionExtended;
  pendingFinalize.slowExtractionGuardEnabled =
      session.config.slowExtractionGuardEnabled;
  pendingFinalize.slowExtractionExtended = session.slowExtractionExtended;
  pendingFinalize.targetReachedEarly = session.targetReachedEarly;
  pendingFinalize.maxRecoveryWeightG = session.config.maxRecoveryWeightG;
  pendingFinalize.minBbwBrewTimeMs = session.config.minBbwBrewTimeMs;
  if (session.targetReachedAtMs != 0 &&
      static_cast<int32_t>(session.targetReachedAtMs - shotAnchorMs) >= 0) {
    pendingFinalize.targetReachedEarlyDs = static_cast<uint16_t>(
        (session.targetReachedAtMs - shotAnchorMs) / 100U);
  }
  pendingFinalize.lastKnownWeightValid =
      session.hasWeightAnchor && isfinite(session.lastAcceptedWeightG);
  pendingFinalize.lastKnownWeightG = session.lastAcceptedWeightG;
  pendingFinalize.activePresetId = session.activePresetId != 0
                                       ? session.activePresetId
                                       : presetBank.activeId;
  pendingFinalize.cycleId = session.id;
  pendingFinalize.retarePerformed = session.retarePerformed;
  pendingFinalize.minRecoveryWeightG = session.config.minRecoveryWeightG;
  pendingFinalize.autoToManualGuardEnabled =
      session.config.autoToManualGuardEnabled;
  pendingFinalize.autoToManualGuardEnforced = session.autoToManualGuardEnforced;
  pendingFinalize.autoToManualGuardArmed = session.autoToManualGuardArmed;
  if (session.autoToManualGuardEnforced) {
    const uint32_t nowMs = millis();
    pendingFinalize.autoToManualGuardRemainingMs =
        static_cast<int32_t>(session.autoToManualGuardDeadlineAtMs - nowMs) <=
                0
            ? 0U
            : session.autoToManualGuardDeadlineAtMs - nowMs;
  }
  pendingFinalize.noScaleShotGuardEnabled =
      noScaleBbwEnabled(runtimeConfig.noScaleBbwMode);
  pendingFinalize.noScaleShotGuardArmed = noScaleShotGuardArmed;
  pendingFinalize.noScaleBbwMode = runtimeConfig.noScaleBbwMode;
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  copyCString(pendingFinalize.scaleProtocol,
              sizeof(pendingFinalize.scaleProtocol), link.protocolName);
  pendingFinalize.scaleProtocol[sizeof(pendingFinalize.scaleProtocol) - 1] =
      '\0';
  if (session.hasWeightAnchor) {
    shotCurveSampler.accept(session.lastAcceptedWeightG, millis());
    shotCurveSampler.captureEnd(millis(), session.lastAcceptedWeightG);
  } else {
    shotCurveSampler.captureEnd(millis());
  }
  pendingFinalize.curve = shotCurveSampler.snapshot();
}

void commitPendingShotLog(const PendingShotFinalize &snapshot, float finalWeightG,
                          bool finalWeightValid,
                          ActualWeightSource weightSource) {
  ShotLogRecord record;
  memset(&record, 0, sizeof(record));
  record.actualWeightCg = SHOT_LOG_WEIGHT_MISSING;
  record.errorCg = SHOT_LOG_WEIGHT_MISSING;
  record.firstDropDs = snapshot.firstDropDs;
  record.avgFlowCgS = SHOT_LOG_METRIC_MISSING;
  record.bootId = snapshot.bootId;
  record.endedAtMs = snapshot.endedAtMs;
  if (g_wallClock.synced()) {
    const uint32_t utcSec = g_wallClock.nowUtcSec(millis());
    const int16_t offset = runtimeConfig.timezoneOffsetMinutes;
    record.endedAtUnixSec = utcSec;
    record.endedAtLocalSec = shotLogLocalSecFromUtc(utcSec, offset);
    record.timezoneOffsetMinutesAtCommit = offset;
    record.hasWallTime = 1;
  } else {
    record.endedAtUnixSec = 0;
    record.endedAtLocalSec = 0;
    record.timezoneOffsetMinutesAtCommit = 0;
    record.hasWallTime = 0;
  }
  record.durationDs = snapshot.durationDs;
  record.goalWeightG = snapshot.goalWeightG;
  record.offsetUsedCg = shotLogWeightToCentigrams(snapshot.weightOffsetG);
  record.shotType = static_cast<uint8_t>(shotLogTypeFromCycle(
      snapshot.finalState, snapshot.startedWithScale, snapshot.timerOnly,
      snapshot.automaticBrew));
  record.cutType = static_cast<uint8_t>(
      shotLogCutFromEndReason(snapshot.endReason));
  record.extractionGuardEnabled = shotLogPackGuardFlags(
      snapshot.extractionGuardEnabled, snapshot.slowExtractionGuardEnabled);
  if (persistedLastShot.valid && persistedLastShot.cycleId == snapshot.cycleId) {
    record.extractionGuardEnabled = shotLogPackRating(
        record.extractionGuardEnabled, persistedLastShot.rating);
  }
  record.extractionExtended = shotLogPackExtendedFlags(
      snapshot.extractionExtended, snapshot.slowExtractionExtended);
  record.stopDetail = static_cast<uint8_t>(shotLogStopDetailFromEndReason(
      snapshot.endReason, snapshot.extractionGuardEnabled,
      snapshot.extractionExtended));
  if (snapshot.extractionGuardEnabled) {
    record.maxRecoveryWeightCg =
        shotLogWeightToCentigrams(snapshot.maxRecoveryWeightG);
    record.minBbwBrewTimeDs =
        static_cast<uint16_t>(snapshot.minBbwBrewTimeMs / 100U);
  } else {
    record.maxRecoveryWeightCg = SHOT_LOG_WEIGHT_MISSING;
    record.minBbwBrewTimeDs = SHOT_LOG_METRIC_MISSING;
  }
  record.targetReachedEarlyDs = snapshot.targetReachedEarlyDs;
  record.actualWeightSource = static_cast<uint8_t>(weightSource);

  if (finalWeightValid) {
    record.actualWeightCg = shotLogWeightToCentigrams(finalWeightG);
    record.errorCg =
        shotLogWeightToCentigrams(finalWeightG -
                           static_cast<float>(snapshot.goalWeightG));
    if (snapshot.firstDropDs != SHOT_LOG_METRIC_MISSING &&
        (weightSource == ActualWeightSource::POST_DRIP ||
         weightSource == ActualWeightSource::LAST_KNOWN)) {
      const float durationS = snapshot.durationDs / 10.0f;
      const float firstDropS = snapshot.firstDropDs / 10.0f;
      const float brewS = durationS - firstDropS;
      const float deltaG = finalWeightG - snapshot.scaleBaselineG;
      if (brewS > 0.5f && deltaG > 0.0f) {
        const float flowGs = deltaG / brewS;
        if (isfinite(flowGs) && flowGs >= 0.0f && flowGs <= 655.35f) {
          record.avgFlowCgS =
              static_cast<uint16_t>(flowGs * 100.0f + 0.5f);
        }
      }
    }
  }

  const uint32_t newId = shotLog.nextRecordId();
  if (!shotLog.append(record, false)) {
    const size_t nextCount =
        shotLog.count() < SHOT_LOG_CAPACITY ? shotLog.count() + 1U
                                            : SHOT_LOG_CAPACITY;
    const size_t blobBytes =
        sizeof(ShotLogHeader) + nextCount * sizeof(ShotLogRecord);
    addDebugEvent(DebugCategory::CONFIG, DebugCode::SHOT_LOG_PERSIST_FAILED,
                  static_cast<int32_t>(blobBytes),
                  static_cast<int32_t>(shotLog.count()));
    return;
  }
  if (persistedLastShot.valid && persistedLastShot.cycleId == snapshot.cycleId) {
    persistedLastShot.shotLogId = newId;
    lastShotStore.adopt(persistedLastShot);
    lastShotNvsDirty = true;
  }
  if (snapshot.curve.count > 0) {
    ShotCurveRecord curve = snapshot.curve;
    curve.shotId = newId;
    if (!shotCurves.append(curve, false)) {
      addDebugEvent(DebugCategory::CONFIG, DebugCode::SHOT_LOG_PERSIST_FAILED,
                    static_cast<int32_t>(sizeof(ShotCurveStore)),
                    static_cast<int32_t>(shotCurves.count()));
    }
  }
}

void queueWebhookEnd(const PendingShotFinalize &snapshot, float finalWeightG,
                     bool finalWeightValid) {
#ifndef SHOT_STOPPER_HOST_TEST
  const WebhookConfig config = networkManager.webhookConfig();
  if (!config.enabled || !config.end || pendingFinalizeIsRinse(snapshot) ||
      brewEndIsAbandonedStart(snapshot.endReason)) {
    return;
  }
  WebhookEvent event = baseWebhookEvent(WebhookEventType::END, snapshot.cycleId,
                                        snapshot.endedAtMs);
  event.durationMs = static_cast<uint32_t>(snapshot.durationDs) * 100U;
  event.firstDropValid = snapshot.firstDropDs != SHOT_LOG_METRIC_MISSING;
  event.firstDropMs = event.firstDropValid
                          ? static_cast<uint32_t>(snapshot.firstDropDs) * 100U
                          : 0U;
  event.weightValid = finalWeightValid && isfinite(finalWeightG);
  event.weightG = event.weightValid ? finalWeightG : 0.0f;
  event.targetWeightG = static_cast<float>(snapshot.goalWeightG);
  event.presetId = snapshot.activePresetId;
  const ShotLogType type = shotLogTypeFromCycle(
      snapshot.finalState, snapshot.startedWithScale, snapshot.timerOnly,
      snapshot.automaticBrew);
  snprintf(event.shotType, sizeof(event.shotType), "%s", shotLogTypeName(type));
  snprintf(event.stopDetail, sizeof(event.stopDetail), "%s",
           shotLogStopDetailName(shotLogStopDetailFromEndReason(
               snapshot.endReason, snapshot.extractionGuardEnabled,
               snapshot.extractionExtended)));
  if (event.weightValid && event.firstDropValid &&
      event.durationMs > event.firstDropMs + 500U) {
    const float brewSeconds =
        static_cast<float>(event.durationMs - event.firstDropMs) / 1000.0f;
    const float delta = finalWeightG - snapshot.scaleBaselineG;
    if (delta > 0.0f && isfinite(delta)) {
      event.averageFlowGps = delta / brewSeconds;
      event.averageFlowValid = isfinite(event.averageFlowGps);
    }
  }
  (void)networkManager.enqueueWebhook(event);
#else
  (void)snapshot;
  (void)finalWeightG;
  (void)finalWeightValid;
#endif
}

void cancelPendingFinalize(const char *reason) {
  if (!pendingFinalize.pending) {
    return;
  }
  const PendingShotFinalize snapshot = pendingFinalize;
  pendingFinalize.pending = false;
  serialTrace(LogLevel::INFO, reason != nullptr
                                  ? reason
                                  : "Previous drip analysis cancelled");
  ActualWeightSource source = ActualWeightSource::NONE;
  float weightG = 0.0f;
  bool valid = false;
  if (snapshot.lastKnownWeightValid && isfinite(snapshot.lastKnownWeightG)) {
    source = ActualWeightSource::LAST_KNOWN;
    weightG = snapshot.lastKnownWeightG;
    valid = true;
  }
  const bool persistEligible =
      snapshot.logEligible &&
      shotLogBbwEligible(snapshot.startedWithScale, snapshot.timerOnly,
                         snapshot.automaticBrew) &&
      shotLogWeightEligible(weightG, valid);
  if (persistEligible && !pendingFinalizeIsRinse(snapshot)) {
    commitPendingShotLog(snapshot, weightG, valid, source);
  }
  // Home last shot is independent of history eligibility (incl. weight < 1 g).
  if (!pendingFinalizeIsRinse(snapshot) &&
      !brewEndIsAbandonedStart(snapshot.endReason)) {
    persistLastShotFromFinalize(snapshot, weightG, valid);
  }
  queueWebhookEnd(snapshot, weightG, valid);
}

void maybeQueueAutoToManualGuardSample(const PendingShotFinalize &snapshot,
                                       float finalWeightG,
                                       bool postDripWeightValid) {
  if (!postDripWeightValid || snapshot.timerOnly ||
      !shotLogBbwEligible(snapshot.startedWithScale, snapshot.timerOnly,
                          snapshot.automaticBrew) ||
      !shotLogWeightEligible(finalWeightG, postDripWeightValid) ||
      snapshot.extractionExtended || snapshot.slowExtractionExtended ||
      snapshot.endReason == EndReason::AUTO_TO_MANUAL_GUARD ||
      pendingFinalizeIsRinse(snapshot) ||
      brewEndIsAbandonedStart(snapshot.endReason) ||
      snapshot.endReason == EndReason::SHORT_SHOT ||
      snapshot.durationDs < (MIN_SHOT_LOG_DURATION_MS / 100U) ||
      !autoToManualGuardSampleErrorOk(finalWeightG, snapshot.goalWeightG)) {
    return;
  }
  ShotPreset *preset = mutableShotPreset(
      presetBank, snapshot.activePresetId != 0 ? snapshot.activePresetId
                                               : presetBank.activeId);
  if (preset == nullptr) {
    serialTrace(LogLevel::WARNING,
                "A->M sample skipped; shot preset no longer exists");
    return;
  }
  pushAutoToManualGuardSample(preset->autoToManualGuardSamplesDs,
                              snapshot.durationDs);
  RuntimeConfig candidate = runtimeConfig;
  applyShotPresetToConfig(activeShotPreset(presetBank), candidate, true);
  if (preset->id == presetBank.activeId) {
    memcpy(candidate.autoToManualGuardSamplesDs, preset->autoToManualGuardSamplesDs,
           sizeof(candidate.autoToManualGuardSamplesDs));
  }
  ++candidate.revision;
  if (candidate.revision == 0) {
    candidate.revision = 1;
  }
  commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_ATM_SAMPLES);
  serialTracef(LogLevel::INFO, "A->M guard sample queued; trend ms=%lu",
               static_cast<unsigned long>(autoToManualGuardTrendMs(
                   preset->autoToManualGuardSamplesDs,
                   preset->operationalWallMs)));
}

void pendingShotFinalizeTask() {
  if (!pendingFinalize.pending ||
      elapsedMs(pendingFinalize.endedAtMs) < pendingFinalize.dripDelayMs) {
    return;
  }

  PendingShotFinalize snapshot = pendingFinalize;
  pendingFinalize.pending = false;

  float finalWeightG = 0.0f;
  bool postDripWeightValid = false;
  if (scaleAvailable() && isfinite(currentWeight) &&
      currentWeightSequence != snapshot.endedWeightSequence &&
      static_cast<int32_t>(currentWeightReceivedAtMs - snapshot.endedAtMs) >
          0 &&
      plausibleSettledBrewWeight(currentWeight, snapshot.lastKnownWeightG,
                                 snapshot.lastKnownWeightValid)) {
    finalWeightG = currentWeight;
    postDripWeightValid = snapshot.startedWithScale;
  }

  if (brewWeightCutSettlesMachineOff(snapshot.endReason)) {
    if (postDripWeightValid) {
      machineNoteSettledWeightCutOff();
    } else {
      machineCancelSettledWeightCutOff();
    }
  }

  ActualWeightSource weightSource = ActualWeightSource::NONE;
  bool logWeightValid = false;
  float logWeightG = 0.0f;
  if (postDripWeightValid) {
    weightSource = ActualWeightSource::POST_DRIP;
    logWeightValid = true;
    logWeightG = finalWeightG;
  } else if (snapshot.lastKnownWeightValid) {
    weightSource = ActualWeightSource::LAST_KNOWN;
    logWeightValid = true;
    logWeightG = snapshot.lastKnownWeightG;
  }

  // Drip delay determines the settled final weight, not extra shot time. The
  // curve was snapshotted when the relay opened, so revise that same endpoint
  // instead of leaving a false flat tail or appending drip-delay samples.
  if (logWeightValid) {
    (void)settleShotCurveEndWeight(snapshot.curve, logWeightG);
  }

  const bool persistEligible =
      snapshot.logEligible &&
      shotLogBbwEligible(snapshot.startedWithScale, snapshot.timerOnly,
                         snapshot.automaticBrew) &&
      shotLogWeightEligible(logWeightG, logWeightValid);

  if (persistEligible && !pendingFinalizeIsRinse(snapshot)) {
    commitPendingShotLog(snapshot, logWeightG, logWeightValid, weightSource);
  }
  // Home last shot always reflects the finished cycle, even when history
  // skips (weight < 1 g, non-AUTO, etc.). Rinses and abandoned starts do not
  // overwrite it.
  if (!pendingFinalizeIsRinse(snapshot) &&
      !brewEndIsAbandonedStart(snapshot.endReason)) {
    persistLastShotFromFinalize(snapshot, logWeightG, logWeightValid);
  }
  queueWebhookEnd(snapshot, logWeightG, logWeightValid);

  maybeQueueAutoToManualGuardSample(snapshot, finalWeightG, postDripWeightValid);

  if (pendingFinalizeIsRinse(snapshot) || !snapshot.offsetAnalysis ||
      snapshot.extractionExtended || snapshot.slowExtractionExtended) {
    return;
  }

  if (!scaleAvailable() || !isfinite(currentWeight) ||
      !isfinite(snapshot.weightOffsetG) ||
      currentWeightSequence == snapshot.endedWeightSequence ||
      static_cast<int32_t>(currentWeightReceivedAtMs - snapshot.endedAtMs) <=
          0) {
    serialTrace(LogLevel::INFO,
                "Final weight unavailable; offset unchanged");
    return;
  }

  finalWeightG = currentWeight;

  if (!shotLogWeightEligible(finalWeightG, true)) {
    serialTrace(LogLevel::INFO,
                "Final weight below history threshold; offset unchanged");
    return;
  }

  serialTracef(LogLevel::INFO,
               "Final weight: %.2fg; cycle goal: %.2fg; cycle offset: %.2fg",
               finalWeightG, snapshot.goalWeightG, snapshot.weightOffsetG);

  const float observedError =
      finalWeightG - snapshot.goalWeightG + snapshot.weightOffsetG;
  if (fabsf(observedError) > MAX_OFFSET_G) {
    serialTrace(LogLevel::INFO, "Shot error too large; offset unchanged");
    return;
  }

  float updatedOffset =
      snapshot.weightOffsetG + finalWeightG - snapshot.goalWeightG;
  if (!isfinite(updatedOffset)) {
    serialTrace(LogLevel::WARNING,
                "Calculated offset outside safe range; offset unchanged");
    return;
  }
  if (updatedOffset < 0.0f) {
    updatedOffset = 0.0f;
  } else if (updatedOffset > MAX_OFFSET_G) {
    updatedOffset = MAX_OFFSET_G;
  }

  ShotPreset *preset = mutableShotPreset(
      presetBank, snapshot.activePresetId != 0 ? snapshot.activePresetId
                                               : presetBank.activeId);
  if (preset == nullptr) {
    serialTrace(LogLevel::WARNING,
                "Offset learn skipped; shot preset no longer exists");
    return;
  }
  preset->weightOffsetG = updatedOffset;
  RuntimeConfig candidate = runtimeConfig;
  applyShotPresetToConfig(activeShotPreset(presetBank), candidate, true);
  ++candidate.revision;
  if (candidate.revision == 0) {
    candidate.revision = 1;
  }
  commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_OFFSET);
  serialTracef(LogLevel::INFO, "New offset pending durable commit: %.2f",
               preset->weightOffsetG);
}

void serviceBullseyeMelody() {
  if (!bullseyeTracker.pending) {
    return;
  }
  const uint32_t nowMs = millis();
  if (!bullseyeMelodyConfig.enabled || !soundAlertsEnabled() ||
      currentAlertOutputChannel() != AlertOutputChannel::BUZZER_ONLY ||
      !localBuzzer.ready || session.active || bullseyeTracker.expired(nowMs)) {
    bullseyeTracker.clear();
    return;
  }
  if (!scaleAvailable() || !currentWeightIsFresh(nowMs) ||
      currentWeightSequence == bullseyeTracker.processedWeightSequence) {
    return;
  }
  if (bullseyeTracker.accept(currentWeight, currentWeightReceivedAtMs,
                              currentWeightSequence,
                              MAX_AUTOMATION_WEIGHT_AGE_MS)) {
    (void)localBuzzer.requestBullseye();
    serialTracef(LogLevel::INFO,
                 "Bullseye: %.2fg stable at target for %lums",
                 static_cast<double>(currentWeight),
                 static_cast<unsigned long>(BULLSEYE_STABILITY_MS));
  }
}

// ---------------------------------------------------------------------------
