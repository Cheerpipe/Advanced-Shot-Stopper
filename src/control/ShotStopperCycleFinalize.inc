// Shot finalization pipeline: late-tare capture, pending-shot scheduling,
// weight tails, log commit, outcome webhooks, and the finalize task. Split
// from ShotStopperCycleRuntime.inc at its original position; included
// directly after it so the translation-unit order is unchanged.

bool endingRinseCycle(EndReason reason) {
  return stopperState == StopperState::RINSE ||
         reason == EndReason::RINSE_COMPLETE;
}

bool pendingFinalizeIsRinse(const PendingShotFinalize &snapshot) {
  return snapshot.endReason == EndReason::RINSE_COMPLETE ||
         snapshot.finalState == StopperState::RINSE;
}

void captureLateTareCompletion(uint32_t throughMs = millis()) {
  if (!session.retarePerformed) return;
  const ScaleEvent result = copyScaleShotTareResult();
  if (!result.writeSucceeded || result.cycleId != session.id ||
      result.cupWeightRequestId != session.retareRequestId) return;
  const uint32_t anchor = session.circuitClosedAtMs != 0U
                              ? session.circuitClosedAtMs : session.startedAtMs;
  if (static_cast<int32_t>(result.receivedAtMs - anchor) < 0 ||
      static_cast<int32_t>(result.receivedAtMs - throughMs) > 0) return;
  if (!session.active &&
      (!pendingFinalize.pending || pendingFinalize.cycleId != session.id ||
       static_cast<int32_t>(result.receivedAtMs - pendingFinalize.endedAtMs) > 0)) return;
  applyShotTareResult(result);
  if (session.retareAtMs != 0) return;
  session.retareAtMs = result.receivedAtMs;
  if (pendingFinalize.pending && pendingFinalize.cycleId == session.id) {
    pendingFinalize.tareAtDs = static_cast<uint16_t>((result.receivedAtMs - anchor) / 100U);
    if (homeCycleResult.cycleId == session.id) homeCycleResult.tareElapsedMs = result.receivedAtMs - anchor;
    PersistedLastShot last;
    {
      const TaskLockGuard lock(shotStoreMutex);
      last = persistedLastShot;
    }
    if (last.valid && last.cycleId == session.id) {
      last.tareElapsedMs = result.receivedAtMs - anchor;
      persistLastShotSnapshot(last);
    }
  }
}

void schedulePendingShotFinalize(EndReason reason, uint32_t durationMs,
                                 const CycleWallTime &end = captureCycleWallTime()) {
  captureLateTareCompletion(end.endedAtMs);
  if (session.hasWeightAnchor) {
    shotCurveSampler.captureEnd(end.endedAtMs, session.lastAcceptedWeightG);
  } else {
    shotCurveSampler.captureEnd(end.endedAtMs);
  }
  if (brewEndIsAbandonedStart(reason) || endingRinseCycle(reason)) {
    return;
  }
  const bool logEligible = shotLogEligible(reason, durationMs);
  const bool offsetAnalysis = shot.automaticBrew && !session.config.timerOnly &&
                              session.calibrationEligible &&
                              !session.extractionExtended &&
                              !session.slowExtractionExtended;
  bool webhookEligible = false;
#ifndef SHOT_STOPPER_HOST_TEST
  const WebhookConfig webhook = networkManager.webhookConfig();
  webhookEligible = webhook.enabled && webhook.end;
#endif
  if (!logEligible && !offsetAnalysis && !webhookEligible) {
    return;
  }

  pendingFinalize = PendingShotFinalize{};
  pendingFinalize.pending = true;
  pendingFinalize.offsetAnalysis = offsetAnalysis;
  pendingFinalize.logEligible = logEligible;
  pendingFinalize.endedAtMs = end.endedAtMs;
  pendingFinalize.endedAtUtcSec = end.utcSec;
  pendingFinalize.endedAtOffsetMinutes = end.offsetMinutes;
  pendingFinalize.endedAtHasWallTime = end.available;
  pendingFinalize.dripDelayMs = session.config.dripDelayMs;
  pendingFinalize.endedWeightSequence = currentWeightSequence;
  pendingFinalize.curveLastSequence = currentWeightSequence;
  pendingFinalize.cycleStartedAtMs =
      session.circuitClosedAtMs != 0U ? session.circuitClosedAtMs : session.startedAtMs;
  pendingFinalize.bootId = copyShotLogBootId();
  pendingFinalize.durationDs =
      static_cast<uint16_t>(durationMs / 100U);
  const uint32_t shotAnchorMs = pendingFinalize.cycleStartedAtMs;
  if (session.firstDropMs != 0 &&
      static_cast<int32_t>(session.firstDropMs - shotAnchorMs) >= 0) {
    pendingFinalize.firstDropDs = static_cast<uint16_t>(
        (session.firstDropMs - shotAnchorMs) / 100U);
  }
  if (session.retareAtMs != 0 &&
      static_cast<int32_t>(session.retareAtMs - shotAnchorMs) >= 0) {
    pendingFinalize.tareAtDs = static_cast<uint16_t>(
        (session.retareAtMs - shotAnchorMs) / 100U);
  }
  pendingFinalize.goalWeightG = session.config.goalWeightG;
  pendingFinalize.weightOffsetG = session.config.weightOffsetG;
  pendingFinalize.bbwAlgorithm = session.config.bbwAlgorithm;
  pendingFinalize.bbwAlpha = session.config.bbwAlpha;
  pendingFinalize.bbwProfileVersion = session.config.bbwProfileVersion;
  pendingFinalize.bbwLearningGeneration = session.config.bbwLearningGeneration;
  pendingFinalize.scaleConnectionGeneration = session.connectionGenerationAtStart;
  pendingFinalize.scaleBaselineReady = session.scaleBaselineReady;
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
  pendingFinalize.protectionMs = session.config.bbwProtectionMs;
  if (session.targetReachedAtMs != 0 &&
      static_cast<int32_t>(session.targetReachedAtMs - shotAnchorMs) >= 0) {
    pendingFinalize.targetReachedEarlyDs = static_cast<uint16_t>(
        (session.targetReachedAtMs - shotAnchorMs) / 100U);
  }
  pendingFinalize.lastKnownWeightValid =
      session.hasWeightAnchor && std::isfinite(session.lastAcceptedWeightG);
  pendingFinalize.lastKnownWeightG = session.lastAcceptedWeightG;
  pendingFinalize.activePresetId = session.activePresetId != 0
                                       ? session.activePresetId
                                       : presetBank.activeId;
  copyCString(pendingFinalize.activePresetName,
              sizeof(pendingFinalize.activePresetName), session.activePresetName);
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
  copyCString(pendingFinalize.scaleName, sizeof(pendingFinalize.scaleName),
              session.scaleName);
  shotCurveSampler.snapshot(pendingFinalize.curve);
}

void commitPendingShotLog(const PendingShotFinalize &snapshot, float finalWeightG,
                          bool finalWeightValid,
                          ActualWeightSource weightSource) {
  TaskLockGuard lock(shotStoreMutex);
  ShotLogRecord record;
  memset(&record, 0, sizeof(record));
  record.actualWeightCg = SHOT_LOG_WEIGHT_MISSING;
  record.errorCg = SHOT_LOG_WEIGHT_MISSING;
  record.firstDropDs = snapshot.firstDropDs;
  record.tareAtDs = snapshot.tareAtDs;
  record.avgFlowCgS = SHOT_LOG_METRIC_MISSING;
  record.bootId = snapshot.bootId;
  record.endedAtMs = snapshot.endedAtMs;
  if (snapshot.endedAtHasWallTime) {
    record.endedAtUnixSec = snapshot.endedAtUtcSec;
    record.endedAtLocalSec = shotLogLocalSecFromUtc(
        snapshot.endedAtUtcSec, snapshot.endedAtOffsetMinutes);
    record.timezoneOffsetMinutesAtCommit = snapshot.endedAtOffsetMinutes;
    record.hasWallTime = 1;
  } else {
    record.endedAtUnixSec = 0;
    record.endedAtLocalSec = 0;
    record.timezoneOffsetMinutesAtCommit = 0;
    record.hasWallTime = 0;
  }
  // The persisted duration has decisecond precision; keep a just-over-12 s
  // shot distinguishable from an old row recorded at exactly 12.0 s.
  record.durationDs = snapshot.durationDs > 120U ? snapshot.durationDs : 121U;
  record.goalWeightG = snapshot.goalWeightG;
  record.offsetUsedCg = shotLogWeightToCentigrams(snapshot.weightOffsetG);
  record.shotType = static_cast<uint8_t>(shotLogTypeFromCycle(
      snapshot.finalState, snapshot.startedWithScale, snapshot.timerOnly,
      snapshot.automaticBrew));
  record.cutType = static_cast<uint8_t>(
      shotLogCutFromEndReason(snapshot.endReason));
  shotLogSetPresetId(record, snapshot.activePresetId);
  copyCString(record.presetName, sizeof(record.presetName),
              snapshot.activePresetName);
  copyCString(record.scaleName, sizeof(record.scaleName), snapshot.scaleName);
  record.extractionGuardEnabled = shotLogPackGuardFlags(
      snapshot.extractionGuardEnabled, snapshot.slowExtractionGuardEnabled);
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
  shotLogSetBbw(record, snapshot.bbwAlgorithm, snapshot.bbwProfileVersion,
                 snapshot.bbwAlpha, snapshot.bbwLearningApplied);

  if (finalWeightValid) {
    record.actualWeightCg = shotLogWeightToCentigrams(finalWeightG);
    if (finalWeightG > 2.0f && record.actualWeightCg <= 200)
      record.actualWeightCg = 201;
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
        if (std::isfinite(flowGs) && flowGs >= 0.0f && flowGs <= 655.35f) {
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
  shotStoreDirtyGeneration.fetch_add(1, std::memory_order_relaxed);
  if (homeCycleResult.cycleId == snapshot.cycleId) {
    record.id = newId;
    homeCycleResult.shotLogId = newId;
    homeCycleResult = shotLogProjectLastShot(record, homeCycleResult);
  }
  if (persistedLastShot.valid && persistedLastShot.cycleId == snapshot.cycleId) {
    PersistedLastShot linked = persistedLastShot;
    linked.shotLogId = newId;
    lastShotStore.advance(linked, runtimeConfig.bbwProtectionMs);
    lastShotNvsDirty = true;
  }
  if (snapshot.curve.count > 0) {
    static SHOT_STOPPER_PSRAM_BSS ShotCurveRecord curve;
    curve = snapshot.curve;
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
#endif
  WebhookEvent event = baseWebhookEvent(WebhookEventType::END, snapshot.cycleId,
                                        snapshot.endedAtMs);
  event.unixSec = snapshot.endedAtHasWallTime ? snapshot.endedAtUtcSec : 0;
  event.durationMs = static_cast<uint32_t>(snapshot.durationDs) * 100U;
  event.firstDropValid = snapshot.firstDropDs != SHOT_LOG_METRIC_MISSING;
  event.firstDropMs = event.firstDropValid
                          ? static_cast<uint32_t>(snapshot.firstDropDs) * 100U
                          : 0U;
  event.weightValid = finalWeightValid && std::isfinite(finalWeightG);
  event.weightG = event.weightValid ? finalWeightG : 0.0f;
  event.targetWeightG = static_cast<float>(snapshot.goalWeightG);
  event.presetId = snapshot.activePresetId;
  copyCString(event.presetName, sizeof(event.presetName),
              snapshot.activePresetName);
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
    if (delta > 0.0f && std::isfinite(delta)) {
      event.averageFlowGps = delta / brewSeconds;
      event.averageFlowValid = std::isfinite(event.averageFlowGps);
    }
  }
#ifndef SHOT_STOPPER_HOST_TEST
  (void)networkManager.enqueueWebhook(event);
#else
  hostWebhookEndEvent = event;
#endif
}

void queueWebhookPresetsChanged() {
#ifndef SHOT_STOPPER_HOST_TEST
  const WebhookConfig config = networkManager.webhookConfig();
  if (!config.enabled || !config.presetChanges) return;
  WebhookEvent event = baseWebhookEvent(WebhookEventType::PRESETS_CHANGED, 0);
  event.presetId = presetBank.activeId;
  event.presetRevision = runtimeConfig.revision;
  event.presetCount = presetBank.count < 8 ? presetBank.count : 8;
  for (uint8_t i = 0; i < event.presetCount; ++i) {
    event.presets[i].id = presetBank.presets[i].id;
    event.presets[i].isFactory = presetBank.presets[i].isFactory;
    copyCString(event.presets[i].name, sizeof(event.presets[i].name),
                presetBank.presets[i].name);
  }
  (void)networkManager.enqueueWebhook(event);
#else
  ++hostPresetWebhookCount;
#endif
}

void queueWebhookQuickSettingsChanged() {
#ifndef SHOT_STOPPER_HOST_TEST
  const WebhookConfig config = networkManager.webhookConfig();
  if (!config.enabled || !config.presetChanges) return;
  RecipeSnapshot recipe;
  recipe.presets = presetBank;
  recipe.runtime = runtimeConfig;
  WebhookEvent event =
      baseWebhookEvent(WebhookEventType::QUICK_SETTINGS_CHANGED, 0);
  event.quickSettings = quickSettingsSnapshot(recipe);
  (void)networkManager.enqueueWebhook(event);
#else
  ++hostQuickSettingsWebhookCount;
#endif
}

void cancelPendingFinalize(const char *reason, uint32_t profileReason) {
  const ProfileControlObservation profile;
  if (!pendingFinalize.pending) {
    return;
  }
  captureLateTareCompletion();
  scaleProfileNoteEvent(ScaleProfileEvent::FINALIZE_CANCELLED, millis(),
                        getScaleLinkSnapshot().connectionGeneration, 0, NAN,
                        pendingFinalize.cycleId, profileReason);
  static SHOT_STOPPER_PSRAM_BSS PendingShotFinalize snapshot;
  snapshot = pendingFinalize;
  pendingFinalize.pending = false;
  serialTrace(LogLevel::INFO, reason != nullptr
                                  ? reason
                                  : "Previous drip analysis cancelled");
  ActualWeightSource source = ActualWeightSource::NONE;
  float weightG = 0.0f;
  bool valid = false;
  if (snapshot.lastKnownWeightValid && std::isfinite(snapshot.lastKnownWeightG)) {
    source = ActualWeightSource::LAST_KNOWN;
    weightG = snapshot.lastKnownWeightG;
    valid = true;
  }
  const bool persistEligible =
      snapshot.logEligible && shotLogWeightEligible(weightG, valid);
  // Keep the internal completed-cycle snapshot for linked metadata.
  if (!pendingFinalizeIsRinse(snapshot) &&
      !brewEndIsAbandonedStart(snapshot.endReason)) {
    persistLastShotFromFinalize(snapshot, weightG, valid);
  }
  if (persistEligible && !pendingFinalizeIsRinse(snapshot))
    commitPendingShotLog(snapshot, weightG, valid, source);
  queueWebhookEnd(snapshot, weightG, valid);
}

void maybeQueueAutoToManualGuardSample(const PendingShotFinalize &snapshot,
                                       float finalWeightG,
                                       bool postDripWeightValid) {
  if (!postDripWeightValid || snapshot.timerOnly ||
      !shotLogBbwEligible(snapshot.startedWithScale, snapshot.timerOnly,
                          snapshot.automaticBrew) ||
      !std::isfinite(finalWeightG) || finalWeightG < 1.0f ||
      snapshot.extractionExtended || snapshot.slowExtractionExtended ||
      snapshot.endReason == EndReason::AUTO_TO_MANUAL_GUARD ||
      snapshot.endReason == EndReason::TOUCH_WEIGHT_FALLBACK ||
      pendingFinalizeIsRinse(snapshot) ||
      brewEndIsAbandonedStart(snapshot.endReason) ||
      snapshot.endReason == EndReason::SHORT_SHOT ||
      static_cast<uint32_t>(snapshot.durationDs) * 100U <= snapshot.protectionMs ||
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

#include "ShotStopperBbwFinalize.inc"

void pendingShotFinalizeTask() {
  if (!pendingFinalize.pending ||
      elapsedMs(pendingFinalize.endedAtMs) < pendingFinalize.dripDelayMs) {
    return;
  }

  captureLateTareCompletion();
  const ProfileControlObservation profile;
  static SHOT_STOPPER_PSRAM_BSS PendingShotFinalize snapshot;
  snapshot = pendingFinalize;
  pendingFinalize.pending = false;

  const float finalWeightG = postStopFinalWeightG(snapshot, currentWeight);
  bool postDripWeightValid = false;
  if (scaleAvailable() && std::isfinite(currentWeight) &&
      currentWeightSequence != snapshot.endedWeightSequence &&
      static_cast<int32_t>(currentWeightReceivedAtMs - snapshot.endedAtMs) >
          0 &&
      std::isfinite(finalWeightG) &&
      plausibleSettledBrewWeight(finalWeightG, snapshot.lastKnownWeightG,
                                 snapshot.lastKnownWeightValid)) {
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

  // Keep the cutoff time and settled-yield annotation separate from observations.
  if (logWeightValid) {
    (void)settleShotCurveEndWeight(snapshot.curve, logWeightG);
    pendingFinalize.curve.ended = snapshot.curve.ended;
  }

  snapshot.bbwLearningApplied =
      learnPendingBbw(snapshot, finalWeightG, postDripWeightValid);

  const bool persistEligible =
      snapshot.logEligible && shotLogWeightEligible(logWeightG, logWeightValid);

  // Keep the internal completed-cycle snapshot for linked metadata.
  if (!pendingFinalizeIsRinse(snapshot) &&
      !brewEndIsAbandonedStart(snapshot.endReason)) {
    persistLastShotFromFinalize(snapshot, logWeightG, logWeightValid);
  }
  if (persistEligible && !pendingFinalizeIsRinse(snapshot))
    commitPendingShotLog(snapshot, logWeightG, logWeightValid, weightSource);
  queueWebhookEnd(snapshot, logWeightG, logWeightValid);

  maybeQueueAutoToManualGuardSample(snapshot, finalWeightG, postDripWeightValid);

}

void serviceBullseyeMelody() {
  if (!bullseyeTracker.pending) {
    return;
  }
  const uint32_t nowMs = millis();
  if (!bullseyeMelodyConfig.enabled || !soundAlertsEnabled() ||
      currentAlertOutputChannel() != AlertOutputChannel::BUZZER_ONLY ||
      !localBuzzer.ready || session.active ||
      cupPresenceState() != CupPresenceState::PRESENT ||
      cupPresencePlacementId() != bullseyeTracker.cupPlacementId) {
    bullseyeTracker.clear();
    return;
  }
  if (currentWeightSequence == bullseyeTracker.processedWeightSequence ||
      !scaleAvailable() || !currentWeightIsFresh(nowMs)) {
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
