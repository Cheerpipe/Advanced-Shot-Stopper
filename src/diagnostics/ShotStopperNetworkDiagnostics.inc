bool debugExportChunk(httpd_req_t *request, const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return true;
  }
  return sendCopiedChunk(request, text, strlen(text)) == ESP_OK;
}
bool debugExportChunkf(httpd_req_t *request, char *buf, size_t cap,
                       const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

bool __attribute__((format(printf, 4, 5)))
debugExportChunkf(httpd_req_t *request, char *buf, size_t cap,
                  const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buf, cap, fmt, args);
  va_end(args);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    return false;
  }
  return debugExportChunk(request, buf);
}

}  // namespace

esp_err_t ShotStopperNetwork::debugExportHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  // Public while the diagnostic page is enabled (it carries no secrets);
  // Admin unlock keeps it available with the page disabled.
  if (!self.diagnosticPageEnabled() && !self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  // DEBUG EXPORT MAINTENANCE: extend sections below when adding diagnostically
  // relevant settings / state. Bump DEBUG_EXPORT_SCHEMA_VERSION on material
  // schema changes. Never include secrets.
  if (!self.lockWorkBufForStatus()) {
    return self.workBufBusy(request);
  }
  NetworkWorkBuf &work = *self.workBuf_;
  self.loadControlStatus(work.control);
  if (self.callbacks_.copyDebugExportExtras != nullptr) {
    self.callbacks_.copyDebugExportExtras(work.debugExport, work.control);
  } else {
    work.debugExport = DebugExportExtras{};
  }
  const ControlStatusSnapshot &c = work.control;
  const DebugExportExtras &x = work.debugExport;
  const NetworkStatusSnapshot network = self.snapshot();
  const TimeStatusSnapshot timeStatus = g_wallClock.snapshot(millis());
  const bool configMutable = controlAllowsConfiguration(c);
  char *buf = work.statusJson;
  constexpr size_t cap = NetworkWorkBuf::kStatusJson;

  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "Content-Disposition",
                     "attachment; filename=\"shotstopper-debug.json\"");

  bool ok = debugExportChunkf(
      request, buf, cap,
      "{\"exportSchemaVersion\":%lu,\"exportedAtUtcSec\":%lu,"
      "\"firmwareVersion\":\"%s\",\"bootId\":%lu,\"machineType\":\"%s\","
      "\"boardArch\":\"%s\",\"sections\":{",
      static_cast<unsigned long>(DEBUG_EXPORT_SCHEMA_VERSION),
      static_cast<unsigned long>(timeStatus.utcSec), FW_VERSION,
      static_cast<unsigned long>(c.bootId), compiledMachineTypeId(),
      FW_BOARD_ARCH_STRING);

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"envelope\":{\"configMutable\":%s,\"liveShot\":%s,"
           "\"snapshotStale\":%s,"
           "\"configRevision\":%lu,\"ringRetainLogLevel\":\"%s\","
           "\"serialLogLevel\":\"%s\",\"serialDebugOutput\":%s,"
           "\"bootState\":\"%s\",\"bootComplete\":%s,\"bootDegraded\":%s,"
           "\"settingsPersistenceReady\":%s,"
           "\"scaleWorkerReady\":%s},",
           configMutable ? "true" : "false",
           (c.activeCycle || c.machineRunning || c.relayClosed) ? "true"
                                                                : "false",
           c.snapshotStale ? "true" : "false",
           static_cast<unsigned long>(c.config.revision),
           logLevelName(
               static_cast<LogLevel>(c.config.ringRetainLogLevel)),
           logLevelName(serialLogLevelFromRuntime(c.config)),
           serialLogLevelFromRuntime(c.config) != LogLevel::NONE ? "true"
                                                                  : "false",
           bootStateName(c.bootState),
           c.bootComplete ? "true" : "false",
           c.bootDegraded ? "true" : "false",
           c.bootCapabilities.settingsPersistenceWorker ? "true" : "false",
           c.scaleWorkerReady ? "true" : "false");

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"runtimeConfig\":{\"revision\":%lu,\"goalWeightG\":%u,"
           "\"weightOffsetG\":%.2f,\"weightOffsetBaselineG\":%.2f,"
           "\"autoTare\":%s,\"postTareBaselineGraceMs\":%lu,"
           "\"timerOnly\":%s,\"brewByWeight\":%s,\"canTareStartTimer\":%s,"
           "\"scaleTimerStopExtraDelayMs\":%lu,\"dripDelayMs\":%lu,"
           "\"soundAlertsMuted\":%s,\"firstDropBeep\":%s,"
           "\"paddleReturnReminderBeep\":%s,"
           "\"paddleReturnReminderIntervalMs\":%lu,"
           "\"paddleReturnReminderMaxDurationMs\":%lu,"
           "\"bbwProtectionMs\":%lu,\"operationalWallMs\":%lu,"
           "\"fastExtractionGuardEnabled\":%s,\"maxRecoveryWeightG\":%.1f,"
           "\"minBbwBrewTimeMs\":%lu,\"slowExtractionGuardEnabled\":%s,"
           "\"minRecoveryWeightG\":%.1f,\"maxBbwBrewTimeMs\":%lu,"
           "\"autoToManualGuardEnabled\":%s,"
           "\"autoToManualGuardLimitMode\":\"%s\","
           "\"autoToManualGuardManualLimitMs\":%lu,"
           "\"autoToManualGuardBaselineMs\":%lu,"
           "\"cupProtectionEnabled\":%s,\"stopIfCupRemoved\":%s,"
           "\"requireCupToStart\":%s,\"avoidAccidentalTouchEnabled\":%s,"
           "\"cupPresentWeightG\":%.1f,\"cupRemovedWeightG\":%.1f,"
           "\"avoidBbwShotWithoutScale\":%s,\"noScaleBbwMode\":\"%s\","
           "\"autoRetare\":%s,"
           "\"retareWindowMs\":%lu,\"minimumCupWeightG\":%.1f,"
           "\"lastShotCooldownMs\":%lu,\"timezoneOffsetMinutes\":%d,"
           "\"serialLogLevel\":\"%s\",\"serialDebugOutput\":%s},",
           static_cast<unsigned long>(c.config.revision),
           static_cast<unsigned>(c.config.goalWeightG),
           static_cast<double>(c.config.weightOffsetG),
           static_cast<double>(c.config.weightOffsetBaselineG),
           c.config.autoTare ? "true" : "false",
           static_cast<unsigned long>(c.config.postTareBaselineGraceMs),
           c.config.timerOnly ? "true" : "false",
           c.config.timerOnly ? "false" : "true",
           c.config.canTareStartTimer ? "true" : "false",
           static_cast<unsigned long>(c.config.scaleTimerStopExtraDelayMs),
           static_cast<unsigned long>(c.config.dripDelayMs),
           c.config.soundAlertsMuted ? "true" : "false",
           c.config.firstDropBeep ? "true" : "false",
           c.config.paddleReturnReminderBeep ? "true" : "false",
           static_cast<unsigned long>(c.config.paddleReturnReminderIntervalMs),
           static_cast<unsigned long>(
               c.config.paddleReturnReminderMaxDurationMs),
           static_cast<unsigned long>(c.config.bbwProtectionMs),
           static_cast<unsigned long>(c.config.operationalWallMs),
           c.config.fastExtractionGuardEnabled ? "true" : "false",
           static_cast<double>(c.config.maxRecoveryWeightG),
           static_cast<unsigned long>(c.config.minBbwBrewTimeMs),
           c.config.slowExtractionGuardEnabled ? "true" : "false",
           static_cast<double>(c.config.minRecoveryWeightG),
           static_cast<unsigned long>(c.config.maxBbwBrewTimeMs),
           c.config.autoToManualGuardEnabled ? "true" : "false",
           autoToManualGuardLimitModeId(c.config.autoToManualGuardLimitMode),
           static_cast<unsigned long>(c.config.autoToManualGuardManualLimitMs),
           static_cast<unsigned long>(c.config.autoToManualGuardBaselineMs),
           c.config.cupProtectionEnabled ? "true" : "false",
           c.config.stopIfCupRemoved ? "true" : "false",
           c.config.requireCupToStart ? "true" : "false",
           c.config.avoidAccidentalTouchEnabled ? "true" : "false",
           static_cast<double>(c.config.cupPresentWeightG),
           static_cast<double>(c.config.cupRemovedWeightG),
           noScaleBbwEnabled(c.config.noScaleBbwMode) ? "true" : "false",
           noScaleBbwModeId(c.config.noScaleBbwMode),
           c.config.autoRetare ? "true" : "false",
           static_cast<unsigned long>(c.config.retareWindowMs),
           static_cast<double>(c.config.minimumCupWeightG),
           static_cast<unsigned long>(c.config.lastShotCooldownMs),
           static_cast<int>(c.config.timezoneOffsetMinutes),
           logLevelName(serialLogLevelFromRuntime(c.config)),
           serialLogLevelFromRuntime(c.config) != LogLevel::NONE ? "true"
                                                                  : "false");

  if (self.callbacks_.copyPresetBank != nullptr) {
    self.callbacks_.copyPresetBank(&work.presetBank);
  } else {
    memset(&work.presetBank, 0, sizeof(work.presetBank));
  }
  ok = ok && debugExportChunk(request, "\"presets\":{\"activeId\":");
  ok = ok &&
       debugExportChunkf(request, buf, cap, "%u,\"items\":[",
                         static_cast<unsigned>(work.presetBank.activeId));
  for (uint8_t i = 0;
       ok && i < work.presetBank.count && i < MAX_SHOT_PRESETS; ++i) {
    const ShotPreset &p = work.presetBank.presets[i];
    char safeName[SHOT_PRESET_NAME_CAPACITY] = {};
    sanitizeJsonEmbed(p.name, safeName, sizeof(safeName));
    ok = debugExportChunkf(
        request, buf, cap,
        "%s{\"id\":%u,\"name\":\"%s\",\"isFactory\":%s,\"brewByWeight\":%s,"
        "\"goalWeightG\":%u,\"minBbwBrewTimeMs\":%lu,\"maxBbwBrewTimeMs\":%lu,"
        "\"maxRecoveryWeightG\":%.1f,\"minRecoveryWeightG\":%.1f,"
        "\"fastExtractionGuardEnabled\":%s,\"slowExtractionGuardEnabled\":%s,"
        "\"autoToManualGuardEnabled\":%s,\"autoToManualGuardSamplesDs\":[%u,%u,"
        "%u,%u,%u]}",
        i == 0 ? "" : ",", static_cast<unsigned>(p.id), safeName,
        p.isFactory ? "true" : "false", p.brewByWeight ? "true" : "false",
        static_cast<unsigned>(p.goalWeightG),
        static_cast<unsigned long>(p.minBbwBrewTimeMs),
        static_cast<unsigned long>(p.maxBbwBrewTimeMs),
        static_cast<double>(p.maxRecoveryWeightG),
        static_cast<double>(p.minRecoveryWeightG),
        p.fastExtractionGuardEnabled ? "true" : "false",
        p.slowExtractionGuardEnabled ? "true" : "false",
        p.autoToManualGuardEnabled ? "true" : "false",
        static_cast<unsigned>(p.autoToManualGuardSamplesDs[0]),
        static_cast<unsigned>(p.autoToManualGuardSamplesDs[1]),
        static_cast<unsigned>(p.autoToManualGuardSamplesDs[2]),
        static_cast<unsigned>(p.autoToManualGuardSamplesDs[3]),
        static_cast<unsigned>(p.autoToManualGuardSamplesDs[4]));
  }
  ok = ok && debugExportChunk(request, "]},");

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"controlSnapshot\":{\"state\":\"%s\",\"activeCycle\":%s,"
           "\"relayClosed\":%s,\"machineRunning\":%s,\"reedOn\":%s,"
           "\"physicalActivatorOn\":%s,\"controlSource\":\"%s\","
           "\"weightStreamState\":\"%s\",\"weightControlState\":\"%s\","
           "\"scaleAvailable\":%s,\"currentWeightValid\":%s,"
           "\"currentWeightG\":%.2f,\"observedWeightValid\":%s,"
           "\"observedWeightG\":%.2f,\"cycleId\":%lu,\"uptimeMs\":%lu,"
           "\"snapshotVersion\":%lu,\"snapshotStale\":%s,"
           "\"loopIntervalGapMs\":%lu,\"loopMaxGapMs\":%lu,"
           "\"loopDeadlineMisses\":%lu,\"scaleWorkerMaxGapMs\":%lu,"
           "\"scaleWorkerDeadlineMisses\":%lu,"
           "\"freeHeapBytes\":%lu,\"minimumFreeHeapBytes\":%lu,"
           "\"noScaleShotGuardEnabled\":%s,\"noScaleShotGuardArmed\":%s,"
           "\"noScaleShotGuardHold\":%s,\"cupStartGuardHold\":%s,"
           "\"cycleExtractionExtended\":%s,\"cycleSlowExtractionExtended\":%s,"
           "\"cycleTargetReachedEarly\":%s,\"cycleAutoToManualGuardArmed\":%s,"
           "\"cycleAutoToManualGuardEnforced\":%s,"
           "\"cycleAccidentalTouchHolding\":%s,"
           "\"cycleAccidentalTouchPhase\":\"%s\","
           "\"cycleAccidentalTouchClass\":\"%s\","
           "\"cycleCupRemovedPending\":%s,\"cycleBbwProtectionEnabled\":%s,"
           "\"cycleBbwProtectionEnded\":%s},",
           stopperStateName(c.state), c.activeCycle ? "true" : "false",
           c.relayClosed ? "true" : "false",
           c.machineRunning ? "true" : "false", c.reedOn ? "true" : "false",
           c.physicalActivatorOn ? "true" : "false",
           controlSourceName(c.source),
           weightStreamStateName(c.weightStreamState),
           weightControlStateName(c.weightControlState),
           c.scaleAvailable ? "true" : "false",
           c.currentWeightValid ? "true" : "false",
           static_cast<double>(c.currentWeightG),
           c.observedWeightValid ? "true" : "false",
           static_cast<double>(c.observedWeightG),
           static_cast<unsigned long>(c.cycleId),
           static_cast<unsigned long>(c.uptimeMs),
           static_cast<unsigned long>(c.snapshotVersion),
           c.snapshotStale ? "true" : "false",
           static_cast<unsigned long>(c.loopIntervalGapMs),
           static_cast<unsigned long>(c.loopMaxGapMs),
           static_cast<unsigned long>(c.loopDeadlineMisses),
           static_cast<unsigned long>(c.scaleWorkerMaxGapMs),
           static_cast<unsigned long>(c.scaleWorkerDeadlineMisses),
           static_cast<unsigned long>(c.freeHeapBytes),
           static_cast<unsigned long>(c.minimumFreeHeapBytes),
           c.noScaleShotGuardEnabled ? "true" : "false",
           c.noScaleShotGuardArmed ? "true" : "false",
           c.noScaleShotGuardHold ? "true" : "false",
           c.cupStartGuardHold ? "true" : "false",
           c.cycleExtractionExtended ? "true" : "false",
           c.cycleSlowExtractionExtended ? "true" : "false",
           c.cycleTargetReachedEarly ? "true" : "false",
           c.cycleAutoToManualGuardArmed ? "true" : "false",
           c.cycleAutoToManualGuardEnforced ? "true" : "false",
           c.cycleAccidentalTouchHolding ? "true" : "false",
           accidentalTouchPhaseName(static_cast<AccidentalTouchPhase>(
               c.cycleAccidentalTouchPhase)),
           accidentalTouchClassName(static_cast<AccidentalTouchClass>(
               c.cycleAccidentalTouchClass)),
           c.cycleCupRemovedPending ? "true" : "false",
           c.cycleBbwProtectionEnabled ? "true" : "false",
           c.cycleBbwProtectionEnded ? "true" : "false");

  if (x.sessionActive) {
    ok = ok &&
         debugExportChunkf(
             request, buf, cap,
             "\"session\":{\"active\":true,\"automaticEnabled\":%s,"
             "\"bbwProtectionEnabled\":%s,\"bbwProtectionEnded\":%s,"
             "\"startedWithScale\":%s,\"scaleWasLost\":%s,"
             "\"cupRemovedPending\":%s,\"extractionExtended\":%s,"
             "\"slowExtractionExtended\":%s,\"targetReachedEarly\":%s,"
             "\"autoToManualGuardArmed\":%s,\"autoToManualGuardEnforced\":%s,"
             "\"accidentalTouchHolding\":%s,\"accidentalTouchPhase\":\"%s\","
             "\"accidentalTouchClass\":\"%s\",\"accidentalTouchPendingCount\":%u,"
             "\"activePresetId\":%u,\"weightControlState\":\"%s\","
             "\"id\":%lu,\"startedAtMs\":%lu,\"firstDropMs\":%lu,"
             "\"autoToManualGuardDeadlineAtMs\":%lu,\"targetReachedAtMs\":%lu},",
             x.sessionAutomaticEnabled ? "true" : "false",
             x.sessionBbwProtectionEnabled ? "true" : "false",
             x.sessionBbwProtectionEnded ? "true" : "false",
             x.sessionStartedWithScale ? "true" : "false",
             x.sessionScaleWasLost ? "true" : "false",
             x.sessionCupRemovedPending ? "true" : "false",
             x.sessionExtractionExtended ? "true" : "false",
             x.sessionSlowExtractionExtended ? "true" : "false",
             x.sessionTargetReachedEarly ? "true" : "false",
             x.sessionAutoToManualGuardArmed ? "true" : "false",
             x.sessionAutoToManualGuardEnforced ? "true" : "false",
             x.sessionAccidentalTouchHolding ? "true" : "false",
             accidentalTouchPhaseName(static_cast<AccidentalTouchPhase>(
                 x.sessionAccidentalTouchPhase)),
             accidentalTouchClassName(static_cast<AccidentalTouchClass>(
                 x.sessionAccidentalTouchClass)),
             static_cast<unsigned>(x.sessionAccidentalTouchPendingCount),
             static_cast<unsigned>(x.sessionActivePresetId),
             weightControlStateName(static_cast<WeightControlState>(
                 x.sessionWeightControlState)),
             static_cast<unsigned long>(x.sessionId),
             static_cast<unsigned long>(x.sessionStartedAtMs),
             static_cast<unsigned long>(x.sessionFirstDropMs),
             static_cast<unsigned long>(x.sessionAutoToManualGuardDeadlineAtMs),
             static_cast<unsigned long>(x.sessionTargetReachedAtMs));
  } else {
    ok = ok && debugExportChunk(request, "\"session\":null,");
  }

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"relay\":{\"state\":\"%s\",\"fault\":\"%s\",\"closed\":%s,"
           "\"commandedClosed\":%s,\"feedbackClosed\":%s,"
           "\"feedbackAvailable\":%s,\"externalSafetyPresent\":%s,"
           "\"watchdogReady\":%s,\"timersReady\":%s,\"tripped\":%s,"
           "\"operationalTripped\":%s,\"generation\":%lu,\"closedAtMs\":%lu,"
           "\"operationalLimitMs\":%lu,\"resetReasonCode\":%lu,"
           "\"unsafeResetCount\":%lu,\"resetRecoveryRequired\":%s,"
           "\"bootLoopDetected\":%s,\"resetHistory\":[",
           relaySafetyStateName(x.relay.state),
           relaySafetyFaultName(x.relay.fault),
           x.relay.closed ? "true" : "false",
           x.relay.commandedClosed ? "true" : "false",
           x.relay.feedbackClosed ? "true" : "false",
           x.relay.feedbackAvailable ? "true" : "false",
           x.relay.externalSafetyPresent ? "true" : "false",
           x.relay.watchdogReady ? "true" : "false",
           x.relay.timersReady ? "true" : "false",
           x.relay.tripped ? "true" : "false",
           x.relay.operationalTripped ? "true" : "false",
           static_cast<unsigned long>(x.relay.generation),
           static_cast<unsigned long>(x.relay.closedAtMs),
           static_cast<unsigned long>(x.relay.operationalLimitMs),
           static_cast<unsigned long>(x.relay.resetReasonCode),
           static_cast<unsigned long>(x.relay.unsafeResetCount),
           x.relay.resetRecoveryRequired ? "true" : "false",
           x.relay.bootLoopDetected ? "true" : "false");
  for (uint8_t i = 0; i < x.relay.resetHistoryCount; ++i) {
    const ResetHistoryEntry &entry = x.relay.resetHistory[i];
    ok = ok && debugExportChunkf(
                   request, buf, cap,
                   "%s{\"reason\":\"%s\",\"uptimeMs\":%lu}",
                   i ? "," : "", safetyResetReasonName(entry.reasonCode),
                   static_cast<unsigned long>(entry.uptimeMs));
  }
  ok = ok && debugExportChunk(request, "]},");

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"machine\":{\"rawActivatorOn\":%s,\"physicalActivatorOn\":%s,"
           "\"machineRunState\":\"%s\",\"machineStartAckPending\":%s,"
           "\"machineStopAckPending\":%s,\"machineOrphanRun\":%s},",
           x.rawActivatorOn ? "true" : "false",
           x.physicalActivatorOn ? "true" : "false",
           machineRunStateName(static_cast<MachineRunState>(x.machineRunState)),
           x.machineStartAckPending ? "true" : "false",
           x.machineStopAckPending ? "true" : "false",
           x.machineOrphanRun ? "true" : "false");

  {
    const bool bbwEnabled = !c.config.timerOnly;
    const bool scaleUsable =
        c.scaleAvailable && c.weightStreamState == WeightStreamState::FRESH;
    const bool lastShotCupRemoved =
        c.lastShot.valid && c.lastShot.endReason == EndReason::CUP_REMOVED;
    ok = ok &&
         debugExportChunkf(
             request, buf, cap,
             "\"guards\":{\"bbwEnabled\":%s,\"scaleUsable\":%s,"
             "\"lastShotCupRemoved\":%s,"
             "\"noScale\":{\"enabled\":%s,\"armed\":%s,\"hold\":%s,"
             "\"mode\":\"%s\",\"cooldownRemainingMs\":%lu,"
             "\"scaleWasAvailable\":%s},"
             "\"atm\":{\"enabled\":%s,\"armed\":%s,\"enforced\":%s,"
             "\"remainingMs\":%lu},"
             "\"slowExtraction\":{\"enabled\":%s,\"extended\":%s,"
             "\"targetReachedEarly\":%s,\"activeStopWeightG\":%.1f},"
             "\"fastExtraction\":{\"enabled\":%s,\"extended\":%s,"
             "\"targetReachedEarly\":%s,\"activeStopWeightG\":%.1f,"
             "\"minBbwBrewTimeRemainingMs\":%lu},"
             "\"accidentalTouch\":{\"enabled\":%s,\"holding\":%s,"
             "\"phase\":\"%s\",\"class\":\"%s\",\"pendingCount\":%u},"
             "\"cupProtection\":{\"enabled\":%s,\"stopIfRemoved\":%s,"
             "\"requireCupToStart\":%s,\"present\":%s,\"startHold\":%s,"
             "\"removedPending\":%s,\"bbwProtectionActive\":%s,"
             "\"bbwProtectionEnded\":%s}},",
             bbwEnabled ? "true" : "false", scaleUsable ? "true" : "false",
             lastShotCupRemoved ? "true" : "false",
             c.noScaleShotGuardEnabled ? "true" : "false",
             c.noScaleShotGuardArmed ? "true" : "false",
             c.noScaleShotGuardHold ? "true" : "false",
             noScaleBbwModeId(c.config.noScaleBbwMode),
             static_cast<unsigned long>(
                 c.noScaleShotGuardCooldownRemainingMs),
             c.noScaleShotGuardScaleWasAvailable ? "true" : "false",
             c.config.autoToManualGuardEnabled ? "true" : "false",
             c.cycleAutoToManualGuardArmed ? "true" : "false",
             c.cycleAutoToManualGuardEnforced ? "true" : "false",
             static_cast<unsigned long>(c.cycleAutoToManualGuardRemainingMs),
             c.config.slowExtractionGuardEnabled ? "true" : "false",
             c.cycleSlowExtractionExtended ? "true" : "false",
             c.cycleTargetReachedEarly ? "true" : "false",
             static_cast<double>(c.cycleActiveStopWeightG),
             c.config.fastExtractionGuardEnabled ? "true" : "false",
             c.cycleExtractionExtended ? "true" : "false",
             c.cycleTargetReachedEarly ? "true" : "false",
             static_cast<double>(c.cycleActiveStopWeightG),
             static_cast<unsigned long>(c.cycleMinBbwBrewTimeRemainingMs),
             c.config.avoidAccidentalTouchEnabled ? "true" : "false",
             c.cycleAccidentalTouchHolding ? "true" : "false",
             accidentalTouchPhaseName(static_cast<AccidentalTouchPhase>(
                 c.cycleAccidentalTouchPhase)),
             accidentalTouchClassName(static_cast<AccidentalTouchClass>(
                 c.cycleAccidentalTouchClass)),
             static_cast<unsigned>(c.cycleAccidentalTouchPendingCount),
             c.config.cupProtectionEnabled ? "true" : "false",
             c.config.stopIfCupRemoved ? "true" : "false",
             c.config.requireCupToStart ? "true" : "false",
             c.cupPresent ? "true" : "false",
             c.cupStartGuardHold ? "true" : "false",
             c.cycleCupRemovedPending ? "true" : "false",
             c.cycleBbwProtectionEnabled && !c.cycleBbwProtectionEnded ? "true"
                                                                       : "false",
             c.cycleBbwProtectionEnded ? "true" : "false");
  }

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"cupPresence\":{\"state\":\"%s\",\"holdTransitions\":%s,"
           "\"inNegativeHole\":%s,\"removedArmed\":%s,"
           "\"removedConfirmations\":%u,\"placeStabilitySamples\":%u,"
           "\"holeWeightG\":%.2f,\"placeCandidateWeightG\":%.2f},",
           cupPresenceStateName(static_cast<CupPresenceState>(x.cupState)),
           x.cupHoldTransitions ? "true" : "false",
           x.cupInNegativeHole ? "true" : "false",
           x.cupRemovedArmed ? "true" : "false",
           static_cast<unsigned>(x.cupRemovedConfirmations),
           static_cast<unsigned>(x.cupPlaceStabilitySamples),
           static_cast<double>(x.cupHoleWeightG),
           static_cast<double>(x.cupPlaceCandidateWeightG));

  char scaleLinkRssiJson[16] = "null";
  if (c.scaleRssiValid) {
    snprintf(scaleLinkRssiJson, sizeof(scaleLinkRssiJson), "%d",
             static_cast<int>(c.scaleRssi));
  }

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"scaleLink\":{\"state\":\"%s\",\"disconnectSequence\":%lu,"
           "\"connectionGeneration\":%lu,\"packetSequence\":%lu,"
           "\"packetGaps\":%lu,\"rejectedPackets\":%lu,\"reconnects\":%lu,"
           "\"recoveredStaleCount\":%lu,\"recoveredStaleMs\":%lu,"
           "\"weightUpdateIntervalMs\":%lu,"
           "\"workerProgressAtMs\":%lu,\"timerValid\":%s,\"timerMs\":%lu,"
           "\"timerAgeMs\":%lu,\"protocol\":\"%s\","
           "\"lastDisconnectReasonName\":\"%s\",\"rssi\":%s},",
           debugExportScaleLinkStateName(x.scaleLinkState),
           static_cast<unsigned long>(x.scaleDisconnectSequence),
           static_cast<unsigned long>(x.scaleConnectionGeneration),
           static_cast<unsigned long>(x.scalePacketSequence),
           static_cast<unsigned long>(c.scalePacketGaps),
           static_cast<unsigned long>(c.scaleRejectedPackets),
           static_cast<unsigned long>(c.scaleReconnects),
           static_cast<unsigned long>(c.scaleRecoveredStaleCount),
           static_cast<unsigned long>(c.scaleRecoveredStaleMs),
           static_cast<unsigned long>(c.scaleWeightUpdateIntervalMs),
           static_cast<unsigned long>(x.scaleWorkerProgressAtMs),
           x.scaleTimerValid ? "true" : "false",
           static_cast<unsigned long>(x.scaleTimerMs),
           static_cast<unsigned long>(x.scaleTimerAgeMs), x.scaleProtocolName,
           scaleDisconnectReasonName(c.scaleLastDisconnectReason),
           scaleLinkRssiJson);

  char safeStaSsid[WIFI_SSID_CAPACITY] = {};
  sanitizeJsonEmbed(network.staSsid, safeStaSsid, sizeof(safeStaSsid));
  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"network\":{\"networkActive\":%s,\"apActive\":%s,"
           "\"wifiConfigured\":%s,\"staState\":\"%s\",\"staIp\":\"%s\","
           "\"ssid\":\"%s\",\"apIp\":\"%s\",\"apClients\":%u,"
           "\"httpActive\":%s,\"staMac\":\"%s\",\"staBssid\":\"%s\","
           "\"apMac\":\"%s\",\"staReconnectHeld\":%s,\"apStartHeld\":%s,"
           "\"httpStartHeld\":%s,\"taskAgeMs\":%lu,\"taskStackMinWords\":%lu,"
           "\"startupFailures\":%lu,\"channel\":%u,\"rssi\":%d,"
           "\"signalQualityPct\":%u,\"configState\":\"%s\","
           "\"ipMode\":\"%s\",\"confirmRemainingMs\":%lu},",
           network.networkActive ? "true" : "false",
           network.apActive ? "true" : "false",
           network.wifiConfigured ? "true" : "false",
           staStateName(network.staState), network.staIp, safeStaSsid,
           network.apIp, static_cast<unsigned>(network.apClients),
           network.httpActive ? "true" : "false", network.staMac,
           network.staBssid, network.apMac,
           network.staReconnectHeld ? "true" : "false",
           network.apStartHeld ? "true" : "false",
           network.httpStartHeld ? "true" : "false",
           static_cast<unsigned long>(network.taskAgeMs),
           static_cast<unsigned long>(network.taskStackMinWords),
           static_cast<unsigned long>(network.startupFailures),
           static_cast<unsigned>(network.channel),
           static_cast<int>(network.staRssi),
           static_cast<unsigned>(network.staSignalQualityPct),
           staConfigStateName(network.staConfigState),
           staIpModeName(network.staIpMode),
           static_cast<unsigned long>(network.confirmRemainingMs));

  char safeActiveServer[NTP_SERVER_HOST_CAPACITY] = {};
  sanitizeJsonEmbed(timeStatus.activeServer, safeActiveServer,
                    sizeof(safeActiveServer));
  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"time\":{\"state\":\"%s\",\"utcSec\":%lu,\"lastSyncAgeMs\":%lu,"
           "\"nextRetryInMs\":%lu,\"activeServer\":\"%s\"},",
           timeSyncStateName(timeStatus.state),
           static_cast<unsigned long>(timeStatus.utcSec),
           static_cast<unsigned long>(timeStatus.lastSyncAgeMs),
           static_cast<unsigned long>(timeStatus.nextRetryInMs),
           safeActiveServer);

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"health\":{\"uptimeMs\":%lu,\"snapshotVersion\":%lu,"
           "\"snapshotStale\":%s,\"loopIntervalGapMs\":%lu,"
           "\"loopMaxGapMs\":%lu,\"loopDeadlineMisses\":%lu,"
           "\"loopMaxExecutionUs\":%lu,"
           "\"scaleWorkerMaxGapMs\":%lu,\"scaleWorkerDeadlineMisses\":%lu,"
           "\"scaleWorkerMaxExecutionUs\":%lu,"
           "\"loopStackMinWords\":%lu,"
           "\"scaleStackMinWords\":%lu,\"freeHeapBytes\":%lu,"
           "\"minimumFreeHeapBytes\":%lu,\"largestFreeHeapBlockBytes\":%lu,"
           "\"psramSizeBytes\":%lu,\"psramFreeBytes\":%lu,"
           "\"psramLargestFreeBlockBytes\":%lu,\"bleHostAllocPsram\":%lu,"
           "\"bleHostAllocFallback\":%lu,\"hciRxDropped\":%lu,"
           "\"hciTxDropped\":%lu,\"bleBackend\":\"%s\","
           "\"bleRuntimeState\":%u,\"bleRuntimeLastError\":%ld,"
           "\"bleRuntimeLastResetReason\":%ld,"
           "\"bleRuntimeSyncGeneration\":%lu,\"bleRuntimeResetCount\":%lu,"
           "\"bleRuntimeHostStackMinWords\":%lu,\"workBufExternal\":%s,"
           "\"jsonArenaExternal\":%s,\"allocExternalFallback\":%lu,"
           "\"heapAlertLatched\":%s,"
           "\"stackAlertLatched\":%s,\"loopGapAlertLatched\":%s,"
           "\"cpuLoad5s\":%.2f,\"cpuLoad1m\":%.2f,\"cpuLoad5m\":%.2f,"
           "\"cpuMhz\":%lu,\"tempC\":%.1f,\"tempPeakC\":%.1f},",
           static_cast<unsigned long>(c.uptimeMs),
           static_cast<unsigned long>(c.snapshotVersion),
           c.snapshotStale ? "true" : "false",
           static_cast<unsigned long>(c.loopIntervalGapMs),
           static_cast<unsigned long>(c.loopMaxGapMs),
           static_cast<unsigned long>(c.loopDeadlineMisses),
           static_cast<unsigned long>(c.loopMaxExecutionUs),
           static_cast<unsigned long>(c.scaleWorkerMaxGapMs),
           static_cast<unsigned long>(c.scaleWorkerDeadlineMisses),
           static_cast<unsigned long>(c.scaleWorkerMaxExecutionUs),
           static_cast<unsigned long>(c.loopStackMinWords),
           static_cast<unsigned long>(c.scaleStackMinWords),
           static_cast<unsigned long>(c.freeHeapBytes),
           static_cast<unsigned long>(c.minimumFreeHeapBytes),
           static_cast<unsigned long>(c.largestFreeHeapBlockBytes),
           static_cast<unsigned long>(c.psramSizeBytes),
           static_cast<unsigned long>(c.psramFreeBytes),
           static_cast<unsigned long>(c.psramLargestFreeBlockBytes),
           static_cast<unsigned long>(c.bleHostAllocPsramCount),
           static_cast<unsigned long>(c.bleHostAllocFallbackCount),
           static_cast<unsigned long>(c.bleHostHciRxDropped),
           static_cast<unsigned long>(c.bleHostHciTxDropped),
           "nimble",
           static_cast<unsigned>(c.bleRuntimeState),
           static_cast<long>(c.bleRuntimeLastError),
           static_cast<long>(c.bleRuntimeLastResetReason),
           static_cast<unsigned long>(c.bleRuntimeSyncGeneration),
           static_cast<unsigned long>(c.bleRuntimeResetCount),
           static_cast<unsigned long>(c.bleRuntimeHostStackMinWords),
           c.workBufExternal ? "true" : "false",
           c.jsonArenaExternal ? "true" : "false",
           static_cast<unsigned long>(c.allocExternalFallbackCount),
           x.healthHeapAlertLatched ? "true" : "false",
           x.healthStackAlertLatched ? "true" : "false",
           x.healthLoopGapAlertLatched ? "true" : "false",
           static_cast<double>(c.hwmon.cpuLoad5s),
           static_cast<double>(c.hwmon.cpuLoad1m),
           static_cast<double>(c.hwmon.cpuLoad5m),
           static_cast<unsigned long>(c.hwmon.cpuMhz),
           static_cast<double>(c.hwmon.tempC),
           static_cast<double>(c.hwmon.tempPeakC));

  if (self.callbacks_.copyTaskProfiler != nullptr) {
    self.callbacks_.copyTaskProfiler(work.taskProfiler);
  } else {
    work.taskProfiler = TaskProfilerSnapshot{};
  }
  {
    size_t used = 0;
    buf[0] = '\0';
    ok = ok && jsonScratchAppend(buf, cap, &used, "\"tasks\":");
    ok = ok &&
         formatTaskProfilerObject(buf, cap, &used, work.taskProfiler);
    ok = ok && jsonScratchAppend(buf, cap, &used, ",");
    ok = ok && debugExportChunk(request, buf);
  }

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"safety\":{\"state\":\"%s\",\"fault\":\"%s\","
           "\"taskWatchdogReady\":%s,\"externalHardware\":%s,"
           "\"recoveryRequired\":%s,\"resetReasonCode\":%lu,"
           "\"unsafeResetCount\":%lu,\"bootLoopDetected\":%s,"
           "\"safetyGeneration\":%lu,\"safetyTimersReady\":%s},",
           relaySafetyStateName(c.safetyState),
           relaySafetyFaultName(c.safetyFault),
           c.taskWatchdogReady ? "true" : "false",
           c.externalSafetyPresent ? "true" : "false",
           c.resetRecoveryRequired ? "true" : "false",
           static_cast<unsigned long>(c.resetReasonCode),
           static_cast<unsigned long>(c.unsafeResetCount),
           c.bootLoopDetected ? "true" : "false",
           static_cast<unsigned long>(c.safetyGeneration),
           c.safetyTimersReady ? "true" : "false");

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"bleCompanion\":{\"enabled\":%s,\"active\":%s,"
           "\"restartRequired\":%s,\"stackReady\":%s,\"advertising\":%s,"
           "\"connected\":%s,\"protocolVersion\":%u,\"acceptedWrites\":%lu,"
           "\"rejectedWrites\":%lu,\"lastRawError\":%ld,"
           "\"advertisingStarts\":%lu,\"advertisingFailures\":%lu,"
           "\"phoneConnects\":%lu,\"phoneDisconnects\":%lu,"
           "\"scanIntensity\":\"%s\"},",
           c.bleCompanionEnabled ? "true" : "false",
           c.bleCompanionActive ? "true" : "false",
           c.bleCompanionRestartRequired ? "true" : "false",
           c.bleCompanionStackReady ? "true" : "false",
           c.bleCompanionAdvertising ? "true" : "false",
           c.bleCompanionConnected ? "true" : "false",
           static_cast<unsigned>(c.bleCompanionProtocolVersion),
           static_cast<unsigned long>(c.bleCompanionAcceptedWrites),
           static_cast<unsigned long>(c.bleCompanionRejectedWrites),
           static_cast<long>(c.bleCompanionLastRawError),
           static_cast<unsigned long>(c.bleCompanionAdvertisingStarts),
           static_cast<unsigned long>(c.bleCompanionAdvertisingFailures),
           static_cast<unsigned long>(c.bleCompanionPhoneConnects),
           static_cast<unsigned long>(c.bleCompanionPhoneDisconnects),
           bleScanIntensityName(clampBleScanIntensity(
               c.bleCompanionScanIntensity)));

  self.buildOtaJson(work.otaJson, NetworkWorkBuf::kOtaJson, controlGateOf(c), c.bootId);
  ok = ok && debugExportChunk(request, "\"ota\":");
  ok = ok && debugExportChunk(request, work.otaJson);
  ok = ok && debugExportChunk(request, ",");

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"lastShot\":{\"valid\":%s,\"goalWeightG\":%u,\"durationMs\":%lu,"
           "\"endReason\":\"%s\",\"shotType\":\"%s\","
           "\"extractionExtended\":%s,\"slowExtractionExtended\":%s},"
           "\"compileFlags\":{\"buzzer\":\"%s\",\"remoteMachineControl\":%s,"
           "\"arch\":\"%s\",\"machineType\":\"%s\",\"stopPulseMs\":%lu,"
           "\"maxSinglePressMs\":%lu,\"development\":%s},",
           c.lastShot.valid ? "true" : "false",
           static_cast<unsigned>(c.lastShot.goalWeightG),
           static_cast<unsigned long>(c.lastShot.durationMs),
           self.endReasonName(c.lastShot.endReason),
           lastShotTypeName(static_cast<LastShotType>(c.lastShot.shotType)),
           c.lastShot.extractionExtended ? "true" : "false",
           c.lastShot.slowExtractionExtended ? "true" : "false",
           compiledBuzzerModeId(),
           REMOTE_MACHINE_CONTROL_ENABLED ? "true" : "false",
           FW_BOARD_ARCH_STRING, compiledMachineTypeId(),
           static_cast<unsigned long>(COMPILED_STOP_PULSE_MS),
           static_cast<unsigned long>(COMPILED_MAX_SINGLE_PRESS_MS),
           DEVELOPMENT_BUILD ? "true" : "false");

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"events\":{\"dropped\":%lu,\"items\":[",
           static_cast<unsigned long>(c.debugEventsDropped));
  if (ok && self.callbacks_.copyDebugEvents != nullptr) {
    uint32_t after = 0;
    bool first = true;
    for (;;) {
      const size_t count = self.callbacks_.copyDebugEvents(
          after, work.logBatch, kNetworkLogBatchSize);
      if (count == 0) {
        break;
      }
      for (size_t index = 0; ok && index < count; ++index) {
        const DebugEvent &event = work.logBatch[index];
        ok = debugExportChunkf(
            request, buf, cap,
            "%s{\"sequence\":%lu,\"atMs\":%lu,\"wallSec\":%lu,"
            "\"level\":\"%s\",\"category\":\"%s\",\"code\":\"%s\","
            "\"argument1\":%ld,\"argument2\":%ld}",
            first ? "" : ",", static_cast<unsigned long>(event.sequence),
            static_cast<unsigned long>(event.atMs),
            static_cast<unsigned long>(event.wallSec),
            logLevelName(event.level), debugCategoryName(event.category),
            debugCodeName(event.code), static_cast<long>(event.argument1),
            static_cast<long>(event.argument2));
        first = false;
        after = event.sequence;
      }
      if (count < kNetworkLogBatchSize) {
        break;
      }
    }
  }
  ok = ok && debugExportChunk(request, "]},");

  const NvsDiagnosticSnapshot nvs = captureNvsDiagnostics();
  ok = ok && debugExportChunkf(
      request, buf, cap,
      "\"nvs\":{\"statsValid\":%s,\"statsErrorCode\":%ld,"
      "\"statsErrorName\":\"%s\",\"partitionBytes\":%lu,"
      "\"expectedPartitionBytes\":%lu,\"layoutExpected\":%s,"
      "\"usedEntries\":%lu,\"freeEntries\":%lu,"
      "\"availableEntries\":%lu,\"totalEntries\":%lu,"
      "\"namespaces\":%lu,\"failures\":%lu,"
      "\"flashIoLockTimeouts\":%lu,\"lastFailure\":",
      nvs.statsValid ? "true" : "false",
      static_cast<long>(nvs.statsErrorCode), nvsErrorName(nvs.statsErrorCode),
      static_cast<unsigned long>(nvs.partitionBytes),
      static_cast<unsigned long>(EXPECTED_NVS_PARTITION_BYTES),
      nvs.partitionBytes == EXPECTED_NVS_PARTITION_BYTES ? "true" : "false",
      static_cast<unsigned long>(nvs.usedEntries),
      static_cast<unsigned long>(nvs.freeEntries),
      static_cast<unsigned long>(nvs.availableEntries),
      static_cast<unsigned long>(nvs.totalEntries),
      static_cast<unsigned long>(nvs.namespaceCount),
      static_cast<unsigned long>(nvs.failureCount),
      static_cast<unsigned long>(flashIoLockTimeouts()));
  if (ok && nvs.lastFailure.present) {
    ok = debugExportChunkf(
        request, buf, cap,
        "{\"subsystem\":\"%s\",\"operation\":\"%s\","
        "\"errorCode\":%ld,\"errorName\":\"%s\","
        "\"requiredEntries\":%lu,\"availableEntriesAtFailure\":%lu}},",
        nvsSubsystemName(nvs.lastFailure.subsystem),
        nvsOperationName(nvs.lastFailure.operation),
        static_cast<long>(nvs.lastFailure.errorCode),
        nvsErrorName(nvs.lastFailure.errorCode),
        static_cast<unsigned long>(nvs.lastFailure.requiredEntries),
        static_cast<unsigned long>(nvs.lastFailure.availableEntriesAtFailure));
  } else if (ok) {
    ok = debugExportChunk(request, "null},");
  }

  size_t shotTotal = 0;
  if (self.callbacks_.copyShotRecords != nullptr) {
    shotTotal =
        self.callbacks_.copyShotRecords(work.shotRecords, SHOT_LOG_CAPACITY);
  }
  const size_t recent =
      shotTotal < DEBUG_EXPORT_SHOT_SUMMARY_LIMIT
          ? shotTotal
          : DEBUG_EXPORT_SHOT_SUMMARY_LIMIT;
  ok = ok &&
       debugExportChunkf(request, buf, cap,
                         "\"shotLogSummary\":{\"total\":%lu,\"recent\":[",
                         static_cast<unsigned long>(shotTotal));
  for (size_t index = 0; ok && index < recent; ++index) {
    const ShotLogRecord &record = work.shotRecords[index];
    ok = debugExportChunkf(
        request, buf, cap,
        "%s{\"id\":%lu,\"bootId\":%lu,\"durationDs\":%u,\"goalWeightG\":%u,"
        "\"stopDetail\":\"%s\"}",
        index == 0 ? "" : ",", static_cast<unsigned long>(record.id),
        static_cast<unsigned long>(record.bootId),
        static_cast<unsigned>(record.durationDs),
        static_cast<unsigned>(record.goalWeightG),
        shotLogStopDetailName(
            static_cast<ShotLogStopDetail>(record.stopDetail)));
  }
  ok = ok && debugExportChunk(request, "]}}}");

  if (!ok) {
    self.unlockWorkBuf();
    return ESP_FAIL;
  }
  const esp_err_t finished = httpd_resp_send_chunk(request, nullptr, 0);
  self.unlockWorkBuf();
  return finished;
}

esp_err_t ShotStopperNetwork::logHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.diagnosticPageEnabled() && !self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  // Bounded diagnostic log: fixed enum-derived messages and numeric
  // arguments; credentials and request payloads are never included.
  uint32_t after = 0;
  const size_t queryLength = httpd_req_get_url_query_len(request);
  if (queryLength > 0 && queryLength < 64) {
    char query[64] = {};
    char value[16] = {};
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "after", value, sizeof(value)) == ESP_OK) {
      char *end = nullptr;
      const unsigned long parsed = strtoul(value, &end, 10);
      if (end != value && *end == '\0') {
        after = static_cast<uint32_t>(parsed);
      }
    }
  }

  if (!self.lockWorkBufForStatus()) {
    return self.workBufBusy(request);
  }
  NetworkWorkBuf &work = *self.workBuf_;
  const size_t count = self.callbacks_.copyDebugEvents(
      after, work.logBatch, LOG_BATCH_SIZE);
  self.loadControlStatus(work.control);
  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Connection", "close");
  char header[96] = {};
  snprintf(header, sizeof(header),
           "{\"dropped\":%lu,\"bootId\":%lu,\"events\":[",
           static_cast<unsigned long>(work.control.debugEventsDropped),
           static_cast<unsigned long>(work.control.bootId));
  if (httpd_resp_send_chunk(request, header, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
    self.unlockWorkBuf();
    return ESP_FAIL;
  }
  for (size_t index = 0; index < count; ++index) {
    const DebugEvent &event = work.logBatch[index];
    char message[128] = {};
    if (event.code == DebugCode::LOG_TEXT) {
      copyCString(message, sizeof(message), event.text);
    } else if (event.code == DebugCode::BOOT_BANNER) {
      snprintf(message, sizeof(message), "Advanced Shot Stopper %s (bootId=%ld)",
               FW_VERSION, static_cast<long>(event.argument1));
    } else if (event.code == DebugCode::STATE_TRANSITION &&
        event.argument1 >=
            static_cast<int32_t>(StopperState::REQUIRES_OFF) &&
        event.argument1 <=
            static_cast<int32_t>(StopperState::MANUAL_NO_SCALE) &&
        event.argument2 >=
            static_cast<int32_t>(StopperState::REQUIRES_OFF) &&
        event.argument2 <=
            static_cast<int32_t>(StopperState::MANUAL_NO_SCALE)) {
      snprintf(message, sizeof(message), "%s -> %s",
               stopperStateName(static_cast<StopperState>(event.argument1)),
               stopperStateName(static_cast<StopperState>(event.argument2)));
    } else if ((event.code == DebugCode::WEB_COMMAND_ACCEPTED ||
                event.code == DebugCode::WEB_COMMAND_REJECTED) &&
               event.argument1 >=
                   static_cast<int32_t>(WebCommandType::REMOTE_ON) &&
               event.argument1 <=
                   static_cast<int32_t>(
                       WebCommandType::MAINTENANCE_COMPLETE)) {
      snprintf(message, sizeof(message), "%s: %s", debugCodeName(event.code),
               webCommandTypeName(
                   static_cast<WebCommandType>(event.argument1)));
    } else if (formatScaleSampleDebugMessage(event, message,
                                             sizeof(message))) {
    } else if (formatPersistDebugMessage(event, message, sizeof(message))) {
    } else if (formatLifecycleDebugMessage(event, message, sizeof(message))) {
    } else {
      copyCString(message, sizeof(message), debugCodeName(event.code));
    }
    snprintf(work.jsonItem, NetworkWorkBuf::kJsonItem,
             "%s{\"sequence\":%lu,\"atMs\":%lu,\"wallSec\":%lu,"
             "\"level\":\"%s\",\"category\":\"%s\",\"code\":%u,"
             "\"message\":",
             index == 0 ? "" : ",",
             static_cast<unsigned long>(event.sequence),
             static_cast<unsigned long>(event.atMs),
             static_cast<unsigned long>(event.wallSec),
             logLevelName(event.level), debugCategoryName(event.category),
             static_cast<unsigned>(event.code));
    if (sendCopiedChunk(request, work.jsonItem, strlen(work.jsonItem)) !=
        ESP_OK) {
      self.unlockWorkBuf();
      return ESP_FAIL;
    }
    if (sendJsonStringChunk(request, message) != ESP_OK) {
      self.unlockWorkBuf();
      return ESP_FAIL;
    }
    snprintf(work.jsonItem, NetworkWorkBuf::kJsonItem,
             ",\"argument1\":%ld,\"argument2\":%ld}",
             static_cast<long>(event.argument1),
             static_cast<long>(event.argument2));
    if (sendCopiedChunk(request, work.jsonItem, strlen(work.jsonItem)) !=
        ESP_OK) {
      self.unlockWorkBuf();
      return ESP_FAIL;
    }
  }
  const bool ok =
      httpd_resp_send_chunk(request, "]}", HTTPD_RESP_USE_STRLEN) == ESP_OK &&
      httpd_resp_send_chunk(request, nullptr, 0) == ESP_OK;
  self.unlockWorkBuf();
  return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t ShotStopperNetwork::shotsHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  size_t offset = 0;
  size_t limit = SHOT_LOG_PAGE_DEFAULT;
  ShotLogSort sort = ShotLogSort::Date;
  ShotLogSortDir dir = ShotLogSortDir::Desc;
  parseShotsPageQuery(request, offset, limit, sort, dir);
  if (!self.lockWorkBufForStatus()) {
    return self.workBufBusy(request);
  }
  NetworkWorkBuf &work = *self.workBuf_;
  self.loadControlStatus(work.control);
  const size_t count =
      self.callbacks_.copyShotRecords != nullptr
          ? self.callbacks_.copyShotRecords(work.shotRecords, SHOT_LOG_CAPACITY)
          : 0;
  const size_t curveCount =
      self.callbacks_.copyShotCurves != nullptr
          ? self.callbacks_.copyShotCurves(work.shotCurves, SHOT_CURVE_CAPACITY)
          : 0;
  shotLogSortRecords(work.shotRecords, count, sort, dir);
  size_t start = 0;
  size_t pageCount = 0;
  const bool hasMore =
      shotLogPageSlice(count, offset, limit, start, pageCount);

  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  char header[160] = {};
  snprintf(header, sizeof(header),
           "{\"bootId\":%lu,\"total\":%u,\"offset\":%u,\"limit\":%u,"
           "\"hasMore\":%s,\"shots\":[",
           static_cast<unsigned long>(work.control.bootId),
           static_cast<unsigned>(count), static_cast<unsigned>(start),
           static_cast<unsigned>(limit), hasMore ? "true" : "false");
  if (httpd_resp_send_chunk(request, header, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
    self.unlockWorkBuf();
    return ESP_FAIL;
  }

  for (size_t index = start; index < start + pageCount; ++index) {
    const ShotLogRecord &record = work.shotRecords[index];
    char actual[16] = "null";
    char errorG[16] = "null";
    char errorPct[16] = "null";
    char flow[16] = "null";
    char firstDrop[16] = "null";
    char maxRecovery[16] = "null";
    char minBbwBrewTime[16] = "null";
    char targetEarly[16] = "null";
    if (!shotLogWeightIsMissing(record.actualWeightCg)) {
      snprintf(actual, sizeof(actual), "%.2f",
               static_cast<double>(record.actualWeightCg) / 100.0);
      snprintf(errorG, sizeof(errorG), "%.2f",
               static_cast<double>(record.errorCg) / 100.0);
      if (record.goalWeightG > 0) {
        snprintf(errorPct, sizeof(errorPct), "%.1f",
                 static_cast<double>(record.errorCg) * 100.0 /
                     (static_cast<double>(record.goalWeightG) * 100.0));
      }
    }
    if (record.avgFlowCgS != SHOT_LOG_METRIC_MISSING) {
      snprintf(flow, sizeof(flow), "%.2f",
               static_cast<double>(record.avgFlowCgS) / 100.0);
    }
    if (record.firstDropDs != SHOT_LOG_METRIC_MISSING) {
      snprintf(firstDrop, sizeof(firstDrop), "%.1f",
               static_cast<double>(record.firstDropDs) / 10.0);
    }
    if (!shotLogWeightIsMissing(record.maxRecoveryWeightCg)) {
      snprintf(maxRecovery, sizeof(maxRecovery), "%.2f",
               static_cast<double>(record.maxRecoveryWeightCg) / 100.0);
    }
    if (record.minBbwBrewTimeDs != SHOT_LOG_METRIC_MISSING) {
      snprintf(minBbwBrewTime, sizeof(minBbwBrewTime), "%.1f",
               static_cast<double>(record.minBbwBrewTimeDs) / 10.0);
    }
    if (record.targetReachedEarlyDs != SHOT_LOG_METRIC_MISSING) {
      snprintf(targetEarly, sizeof(targetEarly), "%.1f",
               static_cast<double>(record.targetReachedEarlyDs) / 10.0);
    }
    const ShotCurveRecord emptyCurve = emptyShotCurveRecord();
    const ShotCurveRecord *curve = findShotCurveById(
        work.shotCurves, curveCount, record.id);
    if (curve == nullptr) {
      curve = &emptyCurve;
    }
    char curveJson[640] = {};
    if (!formatShotCurveJsonBody(curveJson, sizeof(curveJson), *curve)) {
      curveJson[0] = '\0';
    }
    snprintf(work.jsonItem, NetworkWorkBuf::kJsonItem,
             "%s{\"id\":%lu,\"bootId\":%lu,\"endedAtMs\":%lu,"
             "\"hasWallTime\":%s,\"endedAtLocalSec\":%lu,"
             "\"endedAtUnixSec\":%lu,\"timezoneOffsetMinutesAtCommit\":%d,"
             "\"durationS\":%.1f,"
             "\"goalG\":%u,\"actualG\":%s,\"errorG\":%s,\"errorPct\":%s,"
             "\"offsetG\":%.2f,\"avgFlowGS\":%s,\"firstDropS\":%s,"
             "\"shotType\":\"%s\",\"cutType\":\"%s\","
             "\"extractionGuardEnabled\":%s,\"extractionExtended\":%s,"
             "\"slowExtractionGuardEnabled\":%s,\"slowExtractionExtended\":%s,"
             "\"stopDetail\":\"%s\",\"maxRecoveryWeightG\":%s,"
             "\"minBbwBrewTimeS\":%s,\"targetReachedEarlyS\":%s,"
             "\"actualWeightSource\":\"%s\",\"rating\":%u,%s}",
             index == start ? "" : ",",
             static_cast<unsigned long>(record.id),
             static_cast<unsigned long>(record.bootId),
             static_cast<unsigned long>(record.endedAtMs),
             record.hasWallTime ? "true" : "false",
             static_cast<unsigned long>(record.endedAtLocalSec),
             static_cast<unsigned long>(record.endedAtUnixSec),
             static_cast<int>(record.timezoneOffsetMinutesAtCommit),
             static_cast<double>(record.durationDs) / 10.0,
             static_cast<unsigned>(record.goalWeightG), actual, errorG,
             errorPct,
             static_cast<double>(record.offsetUsedCg) / 100.0, flow,
             firstDrop,
             shotLogTypeName(static_cast<ShotLogType>(record.shotType)),
             shotLogCutName(static_cast<ShotLogCut>(record.cutType)),
             shotLogFastGuardEnabled(record.extractionGuardEnabled) ? "true"
                                                                    : "false",
             shotLogFastExtended(record.extractionExtended) ? "true" : "false",
             shotLogSlowGuardEnabled(record.extractionGuardEnabled) ? "true"
                                                                    : "false",
             shotLogSlowExtended(record.extractionExtended) ? "true" : "false",
             shotLogStopDetailName(
                 static_cast<ShotLogStopDetail>(record.stopDetail)),
             maxRecovery, minBbwBrewTime, targetEarly,
             actualWeightSourceName(
                 static_cast<ActualWeightSource>(record.actualWeightSource)),
             static_cast<unsigned>(shotLogRating(record.extractionGuardEnabled)),
             curveJson);
    if (sendCopiedChunk(request, work.jsonItem, strlen(work.jsonItem)) !=
        ESP_OK) {
      self.unlockWorkBuf();
      return ESP_FAIL;
    }
  }
  const bool ok =
      httpd_resp_send_chunk(request, "]}", HTTPD_RESP_USE_STRLEN) == ESP_OK &&
      httpd_resp_send_chunk(request, nullptr, 0) == ESP_OK;
  self.unlockWorkBuf();
  return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t ShotStopperNetwork::shotsClearHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  const ControlGateSnapshot status = self.controlGate();
  if (!self.historyMutationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical activator OFF, and wait for Ready before clearing shot history.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "An explicit confirmation is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  char confirmation[32] = {};
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  static const char *const fields[] = {"confirm"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "confirm", confirmation, sizeof(confirmation), false) &&
      strcmp(confirmation, "CLEAR_SHOT_LOG") == 0;
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  memset(confirmation, 0, sizeof(confirmation));
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE,
                     "SHOT_LOG_CLEAR_NOT_CONFIRMED",
                     "The shot-history clear was not explicitly confirmed.");
  }

  if (self.callbacks_.clearShotLog == nullptr ||
      !self.callbacks_.clearShotLog()) {
    return sendError(request, STATUS_UNAVAILABLE, "SHOT_LOG_CLEAR_FAILED",
                     "Shot history could not be cleared. Check Diagnostic → "
                     "NVS for capacity and the last storage error.");
  }
  return sendJson(request, STATUS_OK, "{\"cleared\":true}");
}

esp_err_t ShotStopperNetwork::lastShotClearHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  const ControlGateSnapshot status = self.controlGate();
  if (!self.historyMutationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical activator OFF, and wait for Ready before clearing the last shot.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "An explicit confirmation is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  char confirmation[32] = {};
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  static const char *const fields[] = {"confirm"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "confirm", confirmation, sizeof(confirmation), false) &&
      strcmp(confirmation, "CLEAR_LAST_SHOT") == 0;
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  memset(confirmation, 0, sizeof(confirmation));
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE,
                     "LAST_SHOT_CLEAR_NOT_CONFIRMED",
                     "The last-shot clear was not explicitly confirmed.");
  }

  if (self.callbacks_.clearLastShot == nullptr ||
      !self.callbacks_.clearLastShot()) {
    return sendError(request, STATUS_UNAVAILABLE, "LAST_SHOT_CLEAR_FAILED",
                     "Last shot could not be cleared.");
  }
  return sendJson(request, STATUS_OK, "{\"cleared\":true}");
}

esp_err_t ShotStopperNetwork::shotsDeleteHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  const ControlGateSnapshot status = self.controlGate();
  if (!self.historyMutationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical activator OFF, and wait for Ready before deleting shot records.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "A bounded JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  uint32_t shotId = 0;
  static const char *const fields[] = {"id"};
  const bool parsed = root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
                      jsonUint32(root, "id", shotId) && shotId != 0;
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     "A non-zero shot id is required.");
  }

  if (self.callbacks_.deleteShotRecord == nullptr ||
      !self.callbacks_.deleteShotRecord(shotId)) {
    return sendError(request, STATUS_NOT_FOUND, "SHOT_NOT_FOUND",
                     "The requested shot record was not found.");
  }
  return sendJson(request, STATUS_OK, "{\"deleted\":true}");
}

esp_err_t ShotStopperNetwork::shotsRateHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  const ControlGateSnapshot status = self.controlGate();
  if (!self.historyMutationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical activator OFF, and wait for Ready before rating shots.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "A bounded JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  static const char *const fields[] = {"id", "lastShot", "rating"};
  uint8_t rating = 0;
  uint32_t shotId = 0;
  bool lastShot = false;
  const bool hasId = jsonFieldPresent(root, "id");
  const bool hasLastShot = jsonFieldPresent(root, "lastShot");
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 3) &&
      jsonUint8(root, "rating", rating) && rating <= SHOT_LOG_RATING_MAX &&
      hasId != hasLastShot &&
      (!hasId || (jsonUint32(root, "id", shotId) && shotId != 0)) &&
      (!hasLastShot || (jsonBoolean(root, "lastShot", lastShot) && lastShot));
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     "A rating 0-5 and either a shot id or lastShot is required.");
  }

  if (hasLastShot) {
    if (self.callbacks_.rateLastShot == nullptr ||
        !self.callbacks_.rateLastShot(rating)) {
      return sendError(request, STATUS_NOT_FOUND, "LAST_SHOT_NOT_FOUND",
                       "There is no last shot to rate.");
    }
  } else if (self.callbacks_.rateShotRecord == nullptr ||
             !self.callbacks_.rateShotRecord(shotId, rating)) {
    return sendError(request, STATUS_NOT_FOUND, "SHOT_NOT_FOUND",
                     "The requested shot record was not found.");
  }
  char json[48] = {};
  snprintf(json, sizeof(json), "{\"rated\":true,\"rating\":%u}",
           static_cast<unsigned>(rating));
  return sendJson(request, STATUS_OK, json);
}
