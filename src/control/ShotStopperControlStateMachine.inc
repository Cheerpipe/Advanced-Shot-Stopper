// State machine
// ---------------------------------------------------------------------------

void resetSessionForNewCycle(ControlSource source) {
  bullseyeTracker.clear();
  session = CycleSession{};
  session.id = nextCycleId++;
  if (nextCycleId == 0) {
    nextCycleId = 1;
  }
  session.active = true;
  session.source = source;
  serviceIdleTare();
#if !defined(SHOT_STOPPER_HOST_TEST)
  // Publish the RF gate before any deferred NTP request or webhook can wake a
  // network worker. This is an atomic store/notify only; control never waits.
  networkManager.syncControlCriticalRf(true);
#endif
#if defined(SHOT_STOPPER_HOST_TEST)
  // Host tests mutate RuntimeConfig brew fields directly; keep the active
  // preset (compose source of truth) aligned before snapshotting a cycle.
  ensureShotPresetBank(presetBank, runtimeConfig.retareWindowMs,
                       runtimeConfig.autoRetare);
  {
    ShotPreset &preset = mutableActiveShotPreset(presetBank);
    copyUserRecipeFromConfig(runtimeConfig, preset);
    (preset.bbwAlgorithm == 0 ? preset.weightOffsetG : preset.bbwEwmaOffsetG) =
        runtimeConfig.weightOffsetG;
    memcpy(preset.autoToManualGuardSamplesDs,
           runtimeConfig.autoToManualGuardSamplesDs,
           sizeof(preset.autoToManualGuardSamplesDs));
  }
#endif
  session.config = snapshotConfig(effectiveRuntimeConfig());
  session.activePresetId = presetBank.activeId;
  const ShotPreset &preset = activeShotPreset(presetBank);
  session.config.bbwAlpha = preset.bbwAlgorithm == 0 ? 100 : preset.bbwEwmaAlpha;
  session.config.bbwProfileVersion = preset.bbwProfileVersion;
  session.config.bbwLearningGeneration = bbwLearningBank.forPreset(
      preset.id, presetBank).generations[preset.bbwAlgorithm];
  session.endReason = EndReason::NONE;
}

void maybeRequestNtpSyncOnActivity();
bool beginRinseCycle(ControlSource source);
bool enterRinse();

void beginCycle(ControlSource source = ControlSource::PHYSICAL) {
  if (idleTareBlocksStart()) return;
  refreshLoopGuardsFromLastIntention();
  if (source == ControlSource::PHYSICAL &&
      noScaleShotGuardNeedsFreshActivator) {
    if (!machineLastIntention().turnedOn) {
      return;
    }
    noScaleShotGuardNeedsFreshActivator = false;
  }
  maybeEmitManualNoScaleBeep(loopGuardInputs);
  if (noScaleShotGuardWouldBlock(loopGuardInputs)) {
    if (source == ControlSource::PHYSICAL) {
      noScaleShotGuardHold = true;
      noScaleShotGuardHoldAtMs = millis();
      noScaleShotGuardNeedsFreshActivator = true;
      if (!noScaleBbwRequiresScale(runtimeConfig.noScaleBbwMode) &&
          !machineBlockedStartNeedsHoldQualification()) {
        // WARN_ONCE is consumed by the first rejected start, including a
        // normal momentary tap. Keep the hold latched until release so the
        // same physical activation cannot close K1 after Armed clears.
        consumeNoScaleShotGuard();
        addDebugEvent(DebugCategory::STATE,
                      DebugCode::NO_SCALE_SHOT_GUARD_BLOCKED);
      }
      return;
    }
    blockNoScaleShotGuard();
    return;
  }
  noScaleShotGuardHold = false;

  if (cupStartGuardWouldBlock(loopGuardInputs)) {
    if (source == ControlSource::PHYSICAL) {
      cupStartGuardHold = true;
      cupStartGuardHoldAtMs = millis();
      return;
    }
    cupStartGuardHold = false;
    addDebugEvent(DebugCategory::STATE, DebugCode::CUP_START_GUARD_BLOCKED,
                  weightToCentigrams(currentWeight));
    emitAlert(AlertEvent::CUP_START_BLOCKED);
    return;
  }
  cupStartGuardHold = false;

  flushPendingScaleTimerStopNow();
  cancelPendingFinalize("Previous drip analysis cancelled by a new cycle");

  resetSessionForNewCycle(source);
  session.startedAtMs = millis();
  session.circuitClosedAtMs = 0;
  session.firstDropMs = 0;
  session.retareFlowFirstDetectedAtMs = 0;
  session.scaleBaselineReady = false;
  session.scaleBaselineG = 0.0f;
  session.weightSequenceAtStart = currentWeightSequence;
  const ScaleLinkSnapshot scaleLinkAtStart = getScaleLinkSnapshot();
  session.startedWithScale =
      scaleLinkAvailable(scaleLinkAtStart) && currentWeightIsFresh();
  session.scaleDisconnectSequenceAtStart =
      scaleLinkAtStart.disconnectSequence;
  session.connectionGenerationAtStart =
      scaleLinkAtStart.connectionGeneration;
  session.ownedConnectionGeneration =
      scaleLinkAtStart.connectionGeneration;
  session.weightControlState =
      session.startedWithScale && !session.config.timerOnly
          ? WeightControlState::ACTIVE
          : WeightControlState::INACTIVE;
  session.automaticEnabled =
      session.weightControlState == WeightControlState::ACTIVE;
  session.calibrationEligible = session.automaticEnabled;
  if (session.startedWithScale) {
    resyncCupPresenceIfPanEmpty(currentWeight);
    if (session.config.autoTare) {
      orchestratePostTareBaselineArm();
      markTareZeroReady();
    } else {
      session.hasWeightAnchor = true;
      session.lastAcceptedWeightG = currentWeight;
      session.lastAcceptedWeightAtMs = currentWeightReceivedAtMs;
      session.lastAcceptedPacketSequence = currentWeightSequence;
      session.scaleBaselineG = currentWeight;
      session.scaleBaselineReady = true;
    }
  }
  resetShotTrajectory(session.startedAtMs);
  initializeBbwProtection();

  const bool automaticBbw =
      session.startedWithScale && !session.config.timerOnly;
  machineSetPreferBleAirtime(automaticBbw);
  machineBeginCycle(automaticBbw);
  const uint32_t closeLimitMs =
      automaticBbw ? machineCloseLimitMs(session.config.operationalWallMs)
                   : HARD_MAX_CIRCUIT_CLOSED_MS;
  if (!machineRequestStart(closeLimitMs, source == ControlSource::WEB)) {
    session.active = false;
    session.endReason = EndReason::RELAY_SAFETY_FAILURE;
    machineSetPreferBleAirtime(false);
    machineEndCycle();
    transitionTo(StopperState::REQUIRES_OFF);
    return;
  }
  captureCycleCircuitStart();

  emitCircuitCycleAlert(session.startedWithScale && session.config.autoTare &&
                            session.config.canTareStartTimer
                        ? AlertEvent::TARE_START
                        : AlertEvent::START_TIMER,
                    true);
  if (session.startedWithScale) {
    if (!requestRemoteTimerStart()) {
      session.automaticEnabled = false;
      session.scaleWasLost = true;
      serialTrace(LogLevel::WARNING,
                  "Scale start command unavailable; cycle marked manual");
    }
  }

  enterBrewOrManualFromStart();

#ifndef SHOT_STOPPER_HOST_TEST
  webhookBrewStartPending = true;
  // With real-time delivery selected, do not wait for the normal fresh-scale
  // sample window: the circuit edge itself is the requested start event.
  if (!networkManager.webhookConfig().deferDuringShot) {
    queueWebhookBrewStart();
  }
#endif

  addDebugEvent(DebugCategory::STATE, DebugCode::CYCLE_STARTED,
                weightToCentigrams(
                    static_cast<float>(session.config.goalWeightG)),
                weightToCentigrams(session.config.weightOffsetG));
  maybeRequestNtpSyncOnActivity();
}

bool finalizeCycle(EndReason reason, StopperState nextState,
                   bool webStopActuation = false) {
  const uint32_t durationMs = endedCycleDurationMs();

#ifndef SHOT_STOPPER_HOST_TEST
  if (brewEndIsAbandonedStart(reason) || endingRinseCycle(reason)) {
    webhookBrewStartPending = false;
  } else {
    queueWebhookBrewStart();
  }
#endif

  stopPulseTrains();
  cancelScaleBrewBeep(session.id);
  cancelScaleCompletionBeep();
  // GPIO first, then the machine circuit-open cue. BLE advertising resume is
  // owned by the BLE worker (companion pause + syncScaleRadioCoex).
  if (brewWeightCutSettlesMachineOff(reason)) {
    machineArmSettledWeightCutOff();
  } else {
    machineCancelSettledWeightCutOff();
  }
  const bool rinseEnd = endingRinseCycle(reason);
  bool machineStopAccepted = true;
  if (rinseEnd) {
    machineStopAccepted = machineEndRinse();
  } else if (webStopActuation) {
    machineStopAccepted = machineRequestWebStop();
  } else {
    machineStopAccepted = machineRequestStop();
  }
  rinseClear();
  session.rinseStartedAtMs = 0;
  session.endReason = reason;
  if (!brewEndIsAbandonedStart(reason)) {
    if (shotCompletionGetsLongBeep(reason)) {
      // Completion LONG replaces the stop-timer SINGLE so ends are one cue.
      requestCompletionAlert();
    } else {
      emitCircuitCycleAlert(AlertEvent::STOP_TIMER, true);
    }
  }
  if (reason == EndReason::AUTO_TO_MANUAL_GUARD &&
      runtimeConfig.buzzerAutoToManualGuardEndBeep) {
    emitAlert(AlertEvent::ATM_END);
  }
  scheduleScaleTimerStopAfterCycle(durationMs);

  if (!rinseEnd && !brewEndIsAbandonedStart(reason) &&
      bullseyeMelodyConfig.enabled && soundAlertsEnabled() &&
      currentAlertOutputChannel() == AlertOutputChannel::BUZZER_ONLY &&
      localBuzzer.ready) {
    bullseyeTracker.arm(session.config.goalWeightG, millis(),
                         session.config.dripDelayMs, currentWeightSequence);
  } else {
    bullseyeTracker.clear();
  }

  schedulePendingShotFinalize(reason, durationMs);
#ifndef SHOT_STOPPER_HOST_TEST
  {
    const WebhookConfig webhook = networkManager.webhookConfig();
    if (webhook.enabled && webhook.brewState &&
        !endingRinseCycle(reason) && !brewEndIsAbandonedStart(reason)) {
      WebhookEvent event = baseWebhookEvent(WebhookEventType::IDLE, session.id);
      event.durationMs = durationMs;
      event.targetWeightG = static_cast<float>(session.config.goalWeightG);
      event.presetId = session.activePresetId;
      snprintf(event.stopDetail, sizeof(event.stopDetail), "%s",
               shotLogStopDetailName(shotLogStopDetailFromEndReason(
                   reason, session.config.fastExtractionGuardEnabled,
                   session.extractionExtended)));
      (void)networkManager.enqueueWebhook(event);
    }
  }
#endif
  // Home status must show the last shot immediately (during drip delay),
  // including empty / sub-1 g results. Rinses (including early abort) and
  // abandoned starts do not overwrite last shot.
  if (!rinseEnd && !brewEndIsAbandonedStart(reason)) {
    persistLastShotFromEndedCycle(reason, durationMs);
  }

  lastCycle.valid = true;
  lastCycle.cycleId = session.id;
  lastCycle.durationMs = durationMs;
  lastCycle.endedAtMs = millis();
  lastCycle.endReason = reason;
  lastCycle.source = session.source;
  lastCycle.weightValid =
      currentWeightSequence != session.weightSequenceAtStart &&
      isfinite(currentWeight) &&
      static_cast<int32_t>(currentWeightReceivedAtMs - session.startedAtMs) >=
          0;
  lastCycle.lastWeightG = lastCycle.weightValid ? currentWeight : 0.0f;
  lastCycle.weightAgeAtEndMs =
      lastCycle.weightValid ? elapsedMs(currentWeightReceivedAtMs) : 0;
  lastCycle.weightControlState = session.weightControlState;
  lastCycle.calibrationEligible = session.calibrationEligible;
  if (!rinseEnd && !brewEndIsAbandonedStart(reason)) {
    noScaleShotGuardActivityAtMs = millis();
  }
  session.active = false;
  session.awaitingPostTareBaseline = false;
  holdCupPresenceTransitions(false);
  virtualHoldOn = false;
  machineSetPreferBleAirtime(false);
  machineEndCycle();
  addDebugEvent(DebugCategory::STATE, DebugCode::CYCLE_ENDED,
                static_cast<int32_t>(reason));
  transitionTo(nextState);
  serviceIdleTare();
  return machineStopAccepted;
}

bool beginRinseCycle(ControlSource source) {
  if (idleTareBlocksStart()) return false;
  refreshLoopGuardsFromLastIntention();
  if (noScaleBbwRequiresScale(runtimeConfig.noScaleBbwMode) &&
      noScaleShotGuardWouldBlock(loopGuardInputs)) {
    if (source == ControlSource::WEB) {
      maybeEmitManualNoScaleBeep(loopGuardInputs);
    }
    addDebugEvent(DebugCategory::STATE, DebugCode::NO_SCALE_SHOT_GUARD_BLOCKED);
    return false;
  }
  noScaleShotGuardNeedsFreshActivator = false;
  flushPendingScaleTimerStopNow();
  cancelPendingFinalize(source == ControlSource::WEB
                            ? "Previous drip analysis cancelled by a web rinse"
                            : "Previous drip analysis cancelled by a rinse");
  resetSessionForNewCycle(source);
  session.startedAtMs = millis();
  session.circuitClosedAtMs = 0;
  session.firstDropMs = 0;
  session.retareFlowFirstDetectedAtMs = 0;
  session.scaleBaselineReady = false;
  session.scaleBaselineG = 0.0f;
  session.weightSequenceAtStart = currentWeightSequence;
  session.startedWithScale = scaleAvailable();
  session.automaticEnabled = false;
  virtualHoldOn = false;
  resetShotTrajectory(session.startedAtMs);
  machineSetPreferBleAirtime(false);
  machineBeginCycle(false);
  if (!machineBeginRinse(session.config.operationalWallMs)) {
    session.active = false;
    session.endReason = EndReason::RELAY_SAFETY_FAILURE;
    machineEndCycle();
    transitionTo(StopperState::REQUIRES_OFF);
    return false;
  }
  captureCycleCircuitStart();
  emitCircuitCycleAlert(AlertEvent::START_TIMER, true);
  // Only an Armed no-scale rinse clears the latch; keep Armed when the scale
  // is usable (e.g. web rinse with a connected scale).
  if (noScaleShotGuardArmed) {
    const ScaleLinkSnapshot scaleLink = getScaleLinkSnapshot();
    const bool scaleUsable =
        scaleLinkAvailable(scaleLink) && currentWeightIsFresh();
    if (!scaleUsable &&
        !noScaleBbwRequiresScale(runtimeConfig.noScaleBbwMode)) {
      consumeNoScaleShotGuard();
    }
  }
  if (session.startedWithScale) {
    if (!requestRemoteTimerStart()) {
      session.startedWithScale = false;
      session.automaticEnabled = false;
      session.scaleWasLost = true;
      serialTrace(LogLevel::WARNING,
                  "Scale start command unavailable; rinse marked manual");
    }
  }
  return true;
}

bool enterRinse() {
  if (!machineBeginRinse(session.config.operationalWallMs)) {
    // Do not machineEndRinse(): that would double-pulse on momentary.
    // BREW/MANUAL already get machineRequestStop + machineEndCycle here.
    if (stopperState == StopperState::BREW ||
        stopperState == StopperState::MANUAL_NO_SCALE) {
      finalizeCycle(EndReason::RELAY_SAFETY_FAILURE,
                    StopperState::REQUIRES_OFF);
    } else {
      session.active = false;
      session.endReason = EndReason::RELAY_SAFETY_FAILURE;
      machineEndCycle();
      transitionTo(StopperState::REQUIRES_OFF);
    }
    return false;
  }
  session.rinseStartedAtMs = rinseBegin(session.config.rinseDurationMs);
  session.automaticEnabled = false;
  shot.automaticBrew = false;
  session.calibrationEligible = false;
  session.autoToManualGuardArmed = false;
  session.autoToManualGuardEnforced = false;
  session.awaitingPostTareBaseline = false;
  holdCupPresenceTransitions(false);
  addDebugEvent(DebugCategory::STATE, DebugCode::RINSE_CLASSIFIED);
  transitionTo(StopperState::RINSE);
  maybeRequestNtpSyncOnActivity();
  return true;
}

void enterBrewOrManualFromStart() {
  if (session.config.timerOnly && session.startedWithScale) {
    addDebugEvent(DebugCategory::STATE, DebugCode::TIMER_ONLY_BREW_STARTED);
    transitionTo(StopperState::BREW);
    return;
  }
  if (session.automaticEnabled) {
    shot.automaticBrew = true;
    armAutoToManualGuardForAutomaticBrew();
    addDebugEvent(DebugCategory::STATE, DebugCode::BREW_STARTED);
    transitionTo(StopperState::BREW);
    return;
  }
  session.automaticEnabled = false;
  session.retareEnded = true;
  session.bbwProtectionEnded = true;
  addDebugEvent(DebugCategory::STATE, DebugCode::MANUAL_CYCLE_STARTED);
  transitionTo(StopperState::MANUAL_NO_SCALE);
}

void handleGlobalLimitTrip() {
  const bool wasAlreadyOpenedByTimer = consumeRelaySafetyTrip();
  if (wasAlreadyOpenedByTimer) {
    addDebugEvent(DebugCategory::RELAY, DebugCode::RELAY_OPENED);
  }
  if (!wasAlreadyOpenedByTimer && getRelaySafetySnapshot().closed) {
    machineRequestStop();
  }

  if (!session.active) {
    transitionTo(StopperState::REQUIRES_OFF);
    return;
  }

  finalizeCycle(EndReason::GLOBAL_LIMIT, StopperState::REQUIRES_OFF);
}

void handleOperationalLimitTrip() {
  const bool wasAlreadyOpenedByTimer = consumeOperationalLimitTrip();
  if (wasAlreadyOpenedByTimer) {
    addDebugEvent(DebugCategory::RELAY, DebugCode::RELAY_OPENED);
  }
  addDebugEvent(DebugCategory::SECURITY, DebugCode::OPERATIONAL_LIMIT);
  if (!session.active) {
    transitionTo(StopperState::REQUIRES_OFF);
    return;
  }
  finalizeCycle(EndReason::CONFIGURED_WALL_LIMIT,
                StopperState::REQUIRES_OFF);
}

void stateMachineTask() {
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  if (relay.tripped ||
      (relay.closed &&
       elapsedMs(relay.closedAtMs) >= HARD_MAX_CIRCUIT_CLOSED_MS)) {
    addDebugEvent(DebugCategory::SECURITY, DebugCode::HARD_LIMIT);
    handleGlobalLimitTrip();
    return;
  }
  if (relay.operationalTripped ||
      (relay.closed && relay.operationalLimitMs < HARD_MAX_CIRCUIT_CLOSED_MS &&
       elapsedMs(relay.closedAtMs) >= relay.operationalLimitMs)) {
    handleOperationalLimitTrip();
    return;
  }
  if (relay.closed && session.active &&
      machineCloseLimitMs(session.config.operationalWallMs) ==
          HARD_MAX_CIRCUIT_CLOSED_MS &&
      machineAllowsAutomationStop() &&
      elapsedMs(relay.closedAtMs) >= session.config.operationalWallMs) {
    handleOperationalLimitTrip();
    return;
  }

  const MachineIntention intent = machineLastIntention();

  // The OFF edge that completes the emergency gesture is confirmation only.
  // This also covers momentary builds configured to start on button release.
  if (noScaleRequireBypassCompletedThisLoop) {
    noScaleRequireBypassCompletedThisLoop = false;
    return;
  }

  if (maintenanceLease.active) {
    if (relay.closed) {
      machineRequestStop();
    }
    if (intent.holdActive) {
      if (!maintenanceLease.forwarded) {
        if (maintenanceLease.command.type == WebCommandType::RESTART) {
          // Shot wins: keep the planned restart and let the paddle proceed.
          // Do not report CANCELED — the reboot still happens after idle.
          pendingPlannedRestart = maintenanceLease.command;
          plannedRestartHeld = true;
          maintenanceLease = MaintenanceLease{};
        } else {
          addDebugEvent(DebugCategory::SECURITY,
                        DebugCode::MAINTENANCE_CANCELED,
                        static_cast<int32_t>(maintenanceLease.id));
          maintenanceCancellationCommand = WebCommand{};
          maintenanceCancellationCommand.type =
              WebCommandType::MAINTENANCE_COMPLETE;
          maintenanceCancellationCommand.requestId =
              maintenanceLease.command.requestId;
          maintenanceCancellationCommand.succeeded = false;
          maintenanceCancellationCommand.resultState =
              CommandResultState::CANCELED;
          maintenanceCancellationPending = true;
          maintenanceLease = MaintenanceLease{};
        }
      }
      transitionTo(StopperState::REQUIRES_OFF);
    }
    return;
  }

  if (session.active && session.source == ControlSource::WEB &&
      (intent.turnedOn || intent.turnedOff)) {
    finalizeCycle(EndReason::PHYSICAL_OVERRIDE, nextStateForUserHold(intent));
    return;
  }

  switch (stopperState) {
    case StopperState::REQUIRES_OFF:
      if (intent.intent == UserIntent::STABLE_IDLE || intent.stablyOff) {
        transitionTo(StopperState::READY);
      }
      return;

    case StopperState::READY:
      if (intent.intent == UserIntent::REQUEST_START) {
        beginCycle(ControlSource::PHYSICAL);
      }
      if (intent.intent == UserIntent::REQUEST_RINSE && machineSupportsRinse()) {
        if (noScaleBbwRequiresScale(runtimeConfig.noScaleBbwMode) &&
            noScaleShotGuardNeedsFreshActivator) {
          return;
        }
        noScaleShotGuardHold = false;
        cupStartGuardHold = false;
        if (!beginRinseCycle(ControlSource::PHYSICAL)) {
          return;
        }
        (void)enterRinse();
      }
      return;

    case StopperState::RINSE:
      // Physical edges are consumed by the machine while rinsing.
      if (rinseDeadlineReached()) {
        finalizeCycle(EndReason::RINSE_COMPLETE, nextStateForUserHold(intent));
      }
      return;

    case StopperState::BREW:
      if (intent.intent == UserIntent::REQUEST_RINSE && machineSupportsRinse()) {
        (void)enterRinse();
        return;
      }
      if (intent.intent == UserIntent::REQUEST_STOP) {
        if (session.automaticEnabled) {
          shot.automaticBrew = true;
        }
        finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
        if (!session.active || stopperState != StopperState::BREW) {
          return;
        }
      }

      if (expirePostTareBaselineIfNeeded()) {
        holdCupPresenceTransitions(false);
      }
      serviceBbwProtectionPhases();

      if (session.cupRemovedPending) {
        finalizeCycle(EndReason::CUP_REMOVED, nextStateForUserHold(intent));
        return;
      }
      if (machineTakeNoFlowIdle()) {
        finalizeCycle(EndReason::UNCONFIRMED_START,
                      nextStateForUserHold(intent));
        return;
      }

      if ((session.weightControlState == WeightControlState::ACTIVE ||
           session.weightControlState == WeightControlState::VALIDATING) &&
          scaleAutomationUnavailableForSession()) {
        suspendWeightControl();
        weightStreamState = WeightStreamState::STALE;
        addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_STREAM_STALE);
        serialTrace(LogLevel::WARNING, "Scale stream suspended during brew");
      }

      if (machineAllowsAutomationStop()) {
        const StopperState afterAutomation = nextStateForUserHold(intent);
        if (autoToManualGuardDeadlineDue()) {
          addDebugEvent(DebugCategory::SCALE,
                        DebugCode::AUTO_TO_MANUAL_GUARD_FIRED,
                        static_cast<int32_t>(elapsedMs(session.startedAtMs)));
          finalizeCycle(EndReason::AUTO_TO_MANUAL_GUARD, afterAutomation);
          return;
        }

        if (automaticScaleStopDue()) {
          const EndReason reason = session.directStopPending
                                       ? session.directStopReason
                                       : EndReason::SCALE_THRESHOLD;
          finalizeCycle(reason, afterAutomation);
        }
      }
      return;

    case StopperState::MANUAL_NO_SCALE:
      if (intent.intent == UserIntent::REQUEST_RINSE && machineSupportsRinse()) {
        (void)enterRinse();
        return;
      }
      if (intent.intent == UserIntent::REQUEST_STOP) {
        if (session.automaticEnabled) {
          shot.automaticBrew = true;
        }
        finalizeCycle(EndReason::ACTIVATOR, StopperState::READY);
        return;
      }
      if (machineTakeNoFlowIdle()) {
        finalizeCycle(EndReason::UNCONFIRMED_START,
                      nextStateForUserHold(intent));
        return;
      }
      return;
  }
}

// ---------------------------------------------------------------------------
