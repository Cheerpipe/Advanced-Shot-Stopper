// Placement policy and request provenance stay on the control task. The cup
// detector still owns all presence/stability decisions; the worker owns ATT.
struct IdleTareRuntime {
  uint32_t cupWeightRequestId = 0;
  uint32_t connectionGeneration = 0;
  uint32_t configRevision = 0;
  uint32_t lastSampleAtMs = 0;
  uint32_t lastPacketSequence = 0;
  uint32_t requestId = 0;
  uint32_t requestedAtMs = 0;
  uint32_t placementId = 0;
  uint32_t lastCaptureSequence = 0;
  uint32_t effectAtMs = 0;
  uint32_t lastTerminalRequestId = 0;
  uint32_t lastTerminalAtMs = 0;
  uint32_t lastStartedAtMs = 0;
  uint32_t lastWrittenAtMs = 0;
  uint32_t lastCaptureBoundary = 0;
  uint32_t rejectedSamples = 0;
  uint32_t accessoryPlacementId = 0;
  uint32_t accessoryCycleId = 0;
  uint32_t accessoryStartedAtMs = 0;
  uint32_t accessoryLastAtMs = 0;
  uint32_t unloadStartedAtMs = 0;
  uint32_t unloadLastAtMs = 0;
  float previousReferenceG = 0.0f;
  float requestMinimumG = 0.0f;
  float requestMaximumG = 0.0f;
  float requestPlacementThresholdG = 0.0f;
  float lastWeightG = 0.0f;
  float accessoryMinimumG = 0.0f;
  float accessoryMaximumG = 0.0f;
  float knownAccessoryMassG = 0.0f;
  float unloadMinimumG = 0.0f;
  float unloadMaximumG = 0.0f;
  uint8_t accessorySamples = 0;
  uint8_t unloadSamples = 0;
  bool accessoryUsed = false;
  bool accessoryRemovalAttempted = false;
  bool accessoryRequest = false;
  bool unchangedAfterWrite = false;
  IdleTareReason eligibilityReason = IdleTareReason::NONE;
  IdleTareReason lastReason = IdleTareReason::NONE;
  IdleTareReason deferredReason = IdleTareReason::NONE;
  bool eligible = false;
  bool absentObserved = false;
  bool cupStillPresent = false;
  bool tareNotified = false;
  bool previouslyTared = false;
};
IdleTareRuntime idleTare;
uint32_t nextIdleTareRequestId = 1;

void cancelPendingFinalize(const char *reason);

IdleTareReason idleTareEligibilityReason() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  const bool endedAwaitingRelease =
      stopperState == StopperState::REQUIRES_OFF && lastCycle.valid &&
      lastCycle.cycleId == session.id &&
      !brewEndIsAbandonedStart(lastCycle.endReason) &&
      lastCycle.endReason != EndReason::RINSE_COMPLETE &&
      lastCycle.endReason != EndReason::GLOBAL_LIMIT &&
      lastCycle.endReason != EndReason::CONFIGURED_WALL_LIMIT &&
      lastCycle.endReason != EndReason::RELAY_SAFETY_FAILURE;
  if (!runtimeConfig.autoTareOutsideBrew) return IdleTareReason::FEATURE_DISABLED;
  if (!firmwareInitializationComplete) return IdleTareReason::NOT_READY;
  if (session.active) return IdleTareReason::ACTIVE_CYCLE;
  if (maintenanceLease.active) return IdleTareReason::MAINTENANCE;
  if (relay.closed || relay.tripped || relay.operationalTripped)
    return IdleTareReason::RELAY_BLOCKED;
  if (stopperState != StopperState::READY && !endedAwaitingRelease)
    return IdleTareReason::STOP_STATE;
  if (machineRunState() != MachineRunState::CONFIRMED_OFF)
    return IdleTareReason::MACHINE_NOT_OFF;
  if (!scaleLinkAvailable(getScaleLinkSnapshot()))
    return IdleTareReason::SCALE_UNAVAILABLE;
  return IdleTareReason::NONE;
}

void releaseIdleTareRequest(IdleTareReason reason = IdleTareReason::NONE) {
  IdleTareStatus released;
  if (!cancelIdleScaleTare(idleTare.requestId, &released)) {
    if (idleTare.deferredReason == IdleTareReason::NONE)
      idleTare.deferredReason = reason;
    return;
  }
  finishCupWeight(idleTare.cupWeightRequestId,
      released.phase == IdleTarePhase::SUCCEEDED && idleTare.tareNotified &&
          idleTare.cupStillPresent,
      released.phase == IdleTarePhase::SUCCEEDED ||
          released.reason == IdleTareReason::WRITE_FAILED,
      released.preTareWeightG);
  if (idleTare.accessoryRequest && released.phase == IdleTarePhase::SUCCEEDED &&
      idleTare.tareNotified && cupPresence.weight.valid &&
      std::isfinite(cupPresence.emptyAnchorG)) {
    cupPresence.weight.weightG = -cupPresence.emptyAnchorG;
    idleTare.knownAccessoryMassG = released.preTareWeightG > 0.0f
        ? released.preTareWeightG : 0.0f;
    if (released.preTareWeightG < 0.0f) idleTare.accessoryUsed = false;
    else idleTare.accessoryRemovalAttempted = false;
  }
  if (idleTare.cupStillPresent &&
      cupPresencePlacementId() == idleTare.placementId) {
    if (released.phase == IdleTarePhase::SUCCEEDED) {
      if (idleTare.tareNotified) {
        notifyCupPresenceTare();
        if (!idleTare.accessoryRequest && idleTare.eligible && !session.active) {
          idleTare.accessoryPlacementId = idleTare.placementId;
          idleTare.accessoryCycleId = session.id;
          idleTare.accessoryUsed = false;
          idleTare.accessoryRemovalAttempted = false;
          idleTare.knownAccessoryMassG = 0.0f;
        }
        reason = IdleTareReason::EFFECT_CONFIRMED;
      } else {
        // A still-observed unchanged reference is usable. Missing or different
        // evidence cannot certify either physical tare or empty pan.
        if (!idleTare.unchangedAfterWrite ||
            elapsedMs(idleTare.lastSampleAtMs) > MAX_AUTOMATION_WEIGHT_AGE_MS) {
          markCupTareReferenceUncertain();
        }
        reason = IdleTareReason::EFFECT_UNCONFIRMED;
      }
    } else if (idleTare.tareNotified) {
      restoreCupTareReference(idleTare.previouslyTared,
                              idleTare.previousReferenceG);
    }
  }
  idleTare.lastTerminalRequestId = idleTare.requestId;
  idleTare.lastTerminalAtMs = millis();
  idleTare.lastStartedAtMs = released.startedAtMs;
  idleTare.lastWrittenAtMs = released.writtenAtMs;
  idleTare.lastCaptureBoundary = released.captureBoundary;
  if (idleTare.deferredReason != IdleTareReason::NONE)
    reason = idleTare.deferredReason;
  idleTare.lastReason = reason;
  idleTare.requestId = 0;
  idleTare.tareNotified = false;
  idleTare.cupStillPresent = false;
  idleTare.accessoryRequest = false;
}

void serviceIdleTare() {
  if (!runtimeConfig.autoTareOutsideBrew && idleTare.requestId == 0 &&
      !idleTare.eligible) {
    idleTare.eligibilityReason = IdleTareReason::FEATURE_DISABLED;
    return;
  }
  const uint32_t generation = getScaleLinkSnapshot().connectionGeneration;
  idleTare.eligibilityReason = idleTareEligibilityReason();
  const bool eligible = idleTare.eligibilityReason == IdleTareReason::NONE;
  if (eligible != idleTare.eligible ||
      generation != idleTare.connectionGeneration ||
      runtimeConfig.revision != idleTare.configRevision) {
    idleTare.absentObserved = false;
    idleTare.accessoryPlacementId = 0;
    idleTare.accessorySamples = 0;
    idleTare.unloadSamples = 0;
    idleTare.accessoryRemovalAttempted = false;
    idleTare.knownAccessoryMassG = 0.0f;
    resetCupPlaceStabilityStreak();
    cupPresence.weight.unloadQualified = false;
    cupPresence.weight.unloadSamples = 0;
    const bool changedConnection = generation != idleTare.connectionGeneration;
    const bool changedConfig = runtimeConfig.revision != idleTare.configRevision;
    if (changedConnection) {
      idleTare.lastSampleAtMs = 0;
      idleTare.lastPacketSequence = 0;
      idleTare.lastCaptureSequence = 0;
      idleTare.cupStillPresent = false;
    }
    idleTare.eligible = eligible;
    idleTare.connectionGeneration = generation;
    idleTare.configRevision = runtimeConfig.revision;
    if (idleTare.requestId != 0 &&
        (changedConnection || idleScaleTareStatus().phase == IdleTarePhase::QUEUED)) {
      releaseIdleTareRequest(changedConnection ? IdleTareReason::CONNECTION_CHANGED
                            : changedConfig ? IdleTareReason::CONFIG_CHANGED
                                            : idleTare.eligibilityReason);
    }
  }
  if (idleTare.requestId == 0) return;
  if (cupPresenceState() != CupPresenceState::PRESENT ||
      cupPresencePlacementId() != idleTare.placementId) {
    idleTare.cupStillPresent = false;
  }
  const IdleTareStatus status = idleScaleTareStatus();
  const bool failed = status.requestId != idleTare.requestId ||
                      status.phase == IdleTarePhase::NONE ||
                      status.phase == IdleTarePhase::FAILED;
  if (failed) {
    releaseIdleTareRequest(status.reason == IdleTareReason::NONE
                              ? IdleTareReason::WRITE_FAILED : status.reason);
  } else if (!idleTare.cupStillPresent) {
    releaseIdleTareRequest(IdleTareReason::REMOVED);
  } else if (status.phase == IdleTarePhase::SUCCEEDED && idleTare.tareNotified) {
    releaseIdleTareRequest(IdleTareReason::EFFECT_CONFIRMED);
  } else if (status.phase == IdleTarePhase::QUEUED &&
             elapsedMs(idleTare.lastSampleAtMs) > runtimeConfig.retareStabilityMaxGapMs) {
    releaseIdleTareRequest(IdleTareReason::STALE_SAMPLE);
  } else if (
      elapsedMs(idleTare.requestedAtMs) >=
          SCALE_ATT_TIMEOUT_MS + runtimeConfig.postTareBaselineGraceMs) {
    releaseIdleTareRequest(IdleTareReason::EFFECT_UNCONFIRMED);
  }
}

bool idleTareBlocksStart(bool preempt = false) {
  if (preempt && idleTare.requestId != 0) {
    idleTare.deferredReason = IdleTareReason::START_REQUEST;
    localBuzzer.stopIfCue(BuzzerCue::TARE);
  }
  serviceIdleTare();
  if (idleTare.requestId == 0) return false;
  if (preempt) {
    releaseIdleTareRequest(IdleTareReason::START_REQUEST);
    return false;
  }
  if (idleScaleTareStatus().phase == IdleTarePhase::QUEUED)
    releaseIdleTareRequest(IdleTareReason::START_REQUEST);
  return true;
}

bool observeIdleTareSample(float weight, uint32_t atMs, uint32_t sequence,
                           uint32_t generation, uint32_t captureSequence) {
  serviceIdleTare();
  if (generation != idleTare.connectionGeneration ||
      static_cast<int32_t>(millis() - atMs) < 0 ||
      static_cast<int32_t>(millis() - atMs) >
          static_cast<int32_t>(MAX_AUTOMATION_WEIGHT_AGE_MS) ||
      (idleTare.lastPacketSequence != 0 &&
       (static_cast<int32_t>(sequence - idleTare.lastPacketSequence) <= 0 ||
        static_cast<int32_t>(atMs - idleTare.lastSampleAtMs) < 0))) {
    resetCupSampleEvidence();
    idleTare.unloadSamples = 0;
    ++idleTare.rejectedSamples;
    if (idleTare.requestId != 0 &&
        idleScaleTareStatus().phase == IdleTarePhase::QUEUED)
      releaseIdleTareRequest(IdleTareReason::STALE_SAMPLE);
    return false;
  }
  idleTare.lastSampleAtMs = atMs;
  idleTare.lastPacketSequence = sequence;
  idleTare.lastCaptureSequence = captureSequence;
  idleTare.lastWeightG = weight;
  // Hold a partial negative ramp until it settles; a full unload keeps the
  // cup detector's existing fast path through the empty-pan anchor.
  if (idleTare.knownAccessoryMassG >= runtimeConfig.minimumCupWeightG &&
      weight <= runtimeConfig.cupRemovedWeightG &&
      std::isfinite(cupPresence.emptyAnchorG) &&
      weight > cupPresence.emptyAnchorG + runtimeConfig.retareStabilityToleranceG) {
    if (idleTare.unloadSamples == 0 ||
        atMs - idleTare.unloadLastAtMs > runtimeConfig.retareStabilityMaxGapMs ||
        fmaxf(weight, idleTare.unloadMaximumG) -
            fminf(weight, idleTare.unloadMinimumG) >
            runtimeConfig.retareStabilityToleranceG) {
      idleTare.unloadSamples = 0;
      idleTare.unloadStartedAtMs = atMs;
      idleTare.unloadMinimumG = idleTare.unloadMaximumG = weight;
    }
    idleTare.unloadMinimumG = fminf(weight, idleTare.unloadMinimumG);
    idleTare.unloadMaximumG = fmaxf(weight, idleTare.unloadMaximumG);
    idleTare.unloadLastAtMs = atMs;
    if (idleTare.unloadSamples < UINT8_MAX) ++idleTare.unloadSamples;
  } else {
    idleTare.unloadSamples = 0;
  }
  if (idleTare.requestId != 0 && idleTare.cupStillPresent) {
    const IdleTareStatus status = idleScaleTareStatus();
    if (status.requestId == idleTare.requestId && status.phase == IdleTarePhase::QUEUED) {
      const float low = fminf(idleTare.requestMinimumG, weight);
      const float high = fmaxf(idleTare.requestMaximumG, weight);
      if (weight < MIN_AUTOMATION_WEIGHT_G || weight > MAX_AUTOMATION_WEIGHT_G ||
          weight < idleTare.requestPlacementThresholdG ||
          high - low > runtimeConfig.retareStabilityToleranceG) {
        releaseIdleTareRequest(IdleTareReason::UNSTABLE);
      } else {
        idleTare.requestMinimumG = low;
        idleTare.requestMaximumG = high;
      }
    } else if (status.requestId == idleTare.requestId &&
        (status.phase == IdleTarePhase::WRITING ||
         status.phase == IdleTarePhase::SUCCEEDED)) {
      const bool afterBoundary = captureSequence != 0
          ? static_cast<int32_t>(captureSequence - status.captureBoundary) > 0
          : static_cast<int32_t>(atMs - status.startedAtMs) > 0;
      if (afterBoundary) {
        idleTare.unchangedAfterWrite =
            fmaxf(weight, idleTare.requestMaximumG) -
                fminf(weight, idleTare.requestMinimumG) <=
            runtimeConfig.retareStabilityToleranceG;
        if (!idleTare.tareNotified && fabsf(weight) <= FIRST_DROP_BASELINE_SETTLE_G) {
          notifyCupPresenceTare();
          idleTare.tareNotified = true;
          idleTare.effectAtMs = atMs;
        }
        if (status.phase == IdleTarePhase::SUCCEEDED && idleTare.tareNotified) {
          releaseIdleTareRequest(IdleTareReason::EFFECT_CONFIRMED);
        }
      }
    }
  }
  return true;
}

void handleIdleCupEvent(CupPresenceEvent event, bool accessory = false) {
  if (event == CupPresenceEvent::REMOVED) {
    idleTare.cupStillPresent = false;
    idleTare.accessoryPlacementId = 0;
    idleTare.accessorySamples = 0;
    idleTare.unloadSamples = 0;
    idleTare.accessoryRemovalAttempted = false;
    idleTare.knownAccessoryMassG = 0.0f;
    if (idleTare.requestId != 0) releaseIdleTareRequest(IdleTareReason::REMOVED);
  }
  if (!idleTare.eligible) return;
  if (cupPresenceState() == CupPresenceState::ABSENT) {
    idleTare.absentObserved = cupPresence.weight.emptyValid || cupPresence.weight.unloadQualified;
  }
  if (event != CupPresenceEvent::PLACED && !accessory) return;
  const bool placedInIdle = accessory || idleTare.absentObserved;
  idleTare.absentObserved = false;
  if (!placedInIdle || idleTare.requestId != 0 ||
      !getScaleLinkSnapshot().features.has(ScaleFeatureTare)) {
    idleTare.lastReason = !placedInIdle ? IdleTareReason::NO_ABSENCE
        : idleTare.requestId != 0 ? IdleTareReason::SLOT_BUSY : IdleTareReason::UNSUPPORTED;
    return;
  }
  ScaleCommand command;
  command.type = ScaleCommandType::TARE_ONLY;
  command.autoTare = true;
  command.idleTareRequestId = nextIdleTareRequestId++;
  command.cupWeightRequestId = allocateCupWeightRequestId();
  if (nextIdleTareRequestId == 0) nextIdleTareRequestId = 1;
  command.connectionGeneration = idleTare.connectionGeneration;
  command.expiresAtMs = millis() + MAX_AUTOMATION_WEIGHT_AGE_MS;
  command.qualifiedPacketSequence = idleTare.lastPacketSequence;
  command.commandFeedbackExpected =
      getScaleLinkSnapshot().features.has(ScaleFeatureCommandAudibleFeedback);
  if (!enqueueScaleCommand(command)) {
    idleTare.lastReason = IdleTareReason::QUEUE_FULL;
    return;
  }
  idleTare.requestId = command.idleTareRequestId;
  idleTare.cupWeightRequestId = command.cupWeightRequestId;
  trackCupWeightTare(command.cupWeightRequestId);
  idleTare.requestedAtMs = millis();
  idleTare.cupStillPresent = true;
  idleTare.previouslyTared = cupPresenceIsTared();
  idleTare.previousReferenceG = cupPresenceReferenceG();
  idleTare.requestMinimumG = accessory ? idleTare.accessoryMinimumG : cupPresenceMinimumG();
  idleTare.requestMaximumG = accessory ? idleTare.accessoryMaximumG : cupPresenceMaximumG();
  idleTare.requestPlacementThresholdG = accessory ? runtimeConfig.minimumCupWeightG
                                                : cupPresencePlacementThresholdG();
  if (accessory && idleTare.accessoryMinimumG < 0.0f)
    idleTare.requestPlacementThresholdG =
        cupPresence.emptyAnchorG + runtimeConfig.minimumCupWeightG;
  idleTare.placementId = cupPresencePlacementId();
  idleTare.accessoryRequest = accessory;
  if (accessory) {
    if (idleTare.accessoryMinimumG < 0.0f) idleTare.accessoryRemovalAttempted = true;
    else idleTare.accessoryUsed = true;
  }
  idleTare.unchangedAfterWrite = false;
  idleTare.effectAtMs = 0;
  idleTare.deferredReason = IdleTareReason::NONE;
  idleTare.tareNotified = false;
  emitImmediateCommandAlertIfBuzzer(AlertEvent::TARE);
}

bool idleKnownAccessoryRemoval(float weight) {
  const float toleranceG = runtimeConfig.retareStabilityToleranceG;
  const float cupMassG = -cupPresence.emptyAnchorG - idleTare.knownAccessoryMassG;
  return idleTare.eligible && idleAccessoryRetareEnabled(runtimeConfig.noScaleBbwMode) &&
      idleTare.knownAccessoryMassG >= runtimeConfig.minimumCupWeightG &&
      idleTare.accessoryPlacementId == cupPresencePlacementId() &&
      idleTare.accessoryCycleId == session.id && cupPresenceIsTared() &&
      cupPresence.weight.valid && std::isfinite(cupPresence.emptyAnchorG) &&
      cupMassG >= runtimeConfig.minimumCupWeightG &&
      fabsf(cupMassG - idleTare.knownAccessoryMassG) > 2.0f * toleranceG &&
      weight >= MIN_AUTOMATION_WEIGHT_G &&
      fabsf(weight + idleTare.knownAccessoryMassG) <= toleranceG;
}

bool idleAccessoryUnloadUnsettled(float weight, uint32_t atMs) {
  return idleTare.eligible && idleAccessoryRetareEnabled(runtimeConfig.noScaleBbwMode) &&
      idleTare.knownAccessoryMassG >= runtimeConfig.minimumCupWeightG &&
      idleTare.accessoryPlacementId == cupPresencePlacementId() &&
      idleTare.accessoryCycleId == session.id && cupPresenceIsTared() &&
      cupPresence.weight.valid && std::isfinite(cupPresence.emptyAnchorG) &&
      weight <= runtimeConfig.cupRemovedWeightG &&
      weight > cupPresence.emptyAnchorG + runtimeConfig.retareStabilityToleranceG &&
      (idleTare.unloadSamples < runtimeConfig.retareStabilitySamples ||
       atMs - idleTare.unloadStartedAtMs < runtimeConfig.retareStabilityMinDurationMs);
}

void observeIdleAccessory(float weight, uint32_t atMs) {
  if (idleTare.requestId == 0 && cupPresence.weight.valid &&
      fabsf(weight) <= FIRST_DROP_BASELINE_SETTLE_G) {
    if (idleTare.knownAccessoryMassG == 0.0f) idleTare.accessoryUsed = false;
    idleTare.accessoryRemovalAttempted = false;
  }
  if (!idleTare.eligible || !idleAccessoryRetareEnabled(runtimeConfig.noScaleBbwMode) ||
      idleTare.requestId != 0 ||
      idleTare.accessoryPlacementId != cupPresencePlacementId() ||
      idleTare.accessoryCycleId != session.id || !cupPresenceIsTared() ||
      !cupPresence.weight.valid ||
      !((weight >= runtimeConfig.minimumCupWeightG && !idleTare.accessoryUsed) ||
        (weight <= -runtimeConfig.minimumCupWeightG &&
         !idleTare.accessoryRemovalAttempted && idleKnownAccessoryRemoval(weight))) ||
      weight > MAX_AUTOMATION_WEIGHT_G) {
    idleTare.accessorySamples = 0;
    return;
  }
  if (idleTare.accessorySamples == 0 ||
      atMs - idleTare.accessoryLastAtMs > runtimeConfig.retareStabilityMaxGapMs ||
      fmaxf(weight, idleTare.accessoryMaximumG) -
          fminf(weight, idleTare.accessoryMinimumG) >
          runtimeConfig.retareStabilityToleranceG) {
    idleTare.accessorySamples = 0;
    idleTare.accessoryStartedAtMs = atMs;
    idleTare.accessoryMinimumG = idleTare.accessoryMaximumG = weight;
  }
  idleTare.accessoryMinimumG = fminf(weight, idleTare.accessoryMinimumG);
  idleTare.accessoryMaximumG = fmaxf(weight, idleTare.accessoryMaximumG);
  idleTare.accessoryLastAtMs = atMs;
  if (idleTare.accessorySamples < UINT8_MAX) ++idleTare.accessorySamples;
  if (idleTare.accessorySamples >= runtimeConfig.retareStabilitySamples &&
      atMs - idleTare.accessoryStartedAtMs >= runtimeConfig.retareStabilityMinDurationMs)
    handleIdleCupEvent(CupPresenceEvent::NONE, true);
}
