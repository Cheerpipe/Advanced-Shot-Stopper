namespace {
// Stable field order gives each value one bounded fingerprint. Scratch and TX
// reuse the HTTP workspace; no JSON tree, event queue or new task is allocated.
struct HomeStreamDelta {
  uint32_t *hashes = nullptr;
  size_t capacity = 0, index = 0, used = 0, changed = 0;
  bool snapshot = false, ok = true;

  void field(const char *path, bool value) __attribute__((noinline)) {
    field(path, "%s", value ? "true" : "false");
  }
  void field(const char *path, float value) __attribute__((noinline)) {
    field(path, "%.1f", static_cast<double>(value));
  }

  void field(const char *path, const char *format, ...) __attribute__((format(printf, 3, 4))) {
    if (!ok || index >= capacity) { ok = false; return; }
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(g_work->jsonItem, NetworkWorkBuf::kJsonItem, format, args);
    va_end(args);
    if (length < 0 || static_cast<size_t>(length) >= NetworkWorkBuf::kJsonItem) { ok = false; return; }
    const uint32_t hash = crc32Update(0xFFFFFFFFU,
        reinterpret_cast<const uint8_t *>(g_work->jsonItem), length);
    if (snapshot || hashes[index] != hash) {
      ok = statusJsonAppend(&used, "%s\"%s\":%s", changed ? "," : "", path, g_work->jsonItem);
      hashes[index] = hash;
      ++changed;
    }
    ++index;
  }
};
}  // namespace

// This bounded presentation projection favors flash size over loop unrolling;
// control and safety keep the profile's normal performance optimization.
__attribute__((optimize("Os")))
bool ShotStopperNetwork::sendHomeStream(ShotStreamSession &session,
                                      const ControlStatusSnapshot &control,
                                      const ShotLogRecord *latest) {
  const bool initial = session.homeResync || session.homeBoot != control.bootId;
  HomeStreamDelta delta{session.homeHashes, ShotStreamSession::kHomeFields, 0, 0, 0, initial};
  delta.ok = statusJsonAppend(&delta.used,
      "{\"v\":1,\"type\":\"home\",\"boot\":%lu,\"snapshot\":%s,\"changes\":{",
      static_cast<unsigned long>(control.bootId),
      initial ? "true" : "false");
  const NetworkStatusSnapshot network = snapshot();
  bool unlocked = DEVELOPMENT_BUILD, override = false;
  {
    TaskLockGuard lock(dataMux_);
    unlocked |= adminUnlocked_ && strcmp(adminUnlockClientId_, session.clientId) == 0 &&
        static_cast<int32_t>(millis() - adminUnlockUntilMs_) < 0;
    override = webUiOverrideActive_ && static_cast<int32_t>(millis() - webUiOverrideUntilMs_) < 0;
  }
  delta.field("firmwareVersion", "\"%s\"", FW_VERSION);
  delta.field("bootId", "%lu", static_cast<unsigned long>(control.bootId));
  delta.field("configMutable", bool(controlAllowsConfiguration(control)));
  delta.field("snapshotStale", bool(control.snapshotStale));
  delta.field("webUiOverrideActive", bool(override));
  delta.field("configLockReason", "\"%s\"", configLockReason(control));
  delta.field("liveShot", bool(control.activeCycle || control.relayClosed));
  delta.field("machineType", "\"%s\"", compiledMachineTypeId());
  delta.field("diagnosticPageVisible", bool(diagnosticPageEnabled()));
  delta.field("compatibilityMode", bool(firmwareCompatibilityMode()));
  delta.field("adminUnlocked", bool(unlocked));
  delta.field("development", bool(DEVELOPMENT_BUILD));
  delta.field("lastCommand.requestId", "%lu", static_cast<unsigned long>(network.lastCommandRequestId));
  delta.field("lastCommand.state", "\"%s\"", commandResultStateName(network.lastCommandState));
  const auto &config = control.config;
  const auto clock = g_wallClock.snapshot(millis());
  // Rebase only on connection or clock synchronization; ticking time is local.
  if (initial || session.homeClockSync != clock.lastSyncUtcSec) {
    session.homeClockSync = clock.lastSyncUtcSec;
    session.homeClockUtc = clock.utcSec;
  }
  int16_t timezoneOffset = 0;
  timeZoneOffsetAtOrUtc(config.timezoneId, clock.utcSec, timezoneOffset);
  delta.field("timeUtcSec", "%lu", static_cast<unsigned long>(session.homeClockUtc));
  delta.field("config.appliedTimezoneOffsetMinutes", "%d", static_cast<int>(timezoneOffset));
  delta.field("config.revision", "%lu", static_cast<unsigned long>(config.revision));
  delta.field("config.timezoneId", "\"%s\"", config.timezoneId);
  delta.field("config.timezoneAutomatic", bool(config.timezoneAutomatic));
  delta.field("config.timezoneInitialized", bool(durableTimezoneSaved().load()));
  delta.field("config.soundAlertsEnabled", !bool(config.soundAlertsMuted));
  delta.field("config.alertOutputChannel", "\"%s\"", alertOutputChannelId(config.alertOutputChannel));
  delta.field("config.brewByWeight", !bool(config.timerOnly));
  delta.field("config.goalWeightG", "%u", static_cast<unsigned>(config.goalWeightG));
  delta.field("config.operationalWallMs", "%lu", static_cast<unsigned long>(config.operationalWallMs));
  delta.field("config.minBbwBrewTimeMs", "%lu", static_cast<unsigned long>(config.minBbwBrewTimeMs));
  delta.field("config.maxBbwBrewTimeMs", "%lu", static_cast<unsigned long>(config.maxBbwBrewTimeMs));
  delta.field("config.minRecoveryWeightG", config.minRecoveryWeightG);
  delta.field("config.maxRecoveryWeightG", config.maxRecoveryWeightG);
  delta.field("config.fastExtractionGuardEnabled", bool(config.fastExtractionGuardEnabled));
  delta.field("config.slowExtractionGuardEnabled", bool(config.slowExtractionGuardEnabled));
  delta.field("config.autoToManualGuardEnabled", bool(config.autoToManualGuardEnabled));
  delta.field("config.cupProtectionEnabled", bool(config.cupProtectionEnabled));
  delta.field("config.stopIfCupRemoved", bool(config.stopIfCupRemoved));
  delta.field("config.requireCupToStart", bool(config.requireCupToStart));
  delta.field("config.avoidAccidentalTouchEnabled", bool(config.avoidAccidentalTouchEnabled));
  delta.field("config.touchStopFallbackEnabled", bool(config.touchStopFallbackEnabled));
  delta.field("config.cupPresentWeightG", config.cupPresentWeightG);
  delta.field("config.cupRemovedWeightG", config.cupRemovedWeightG);
  delta.field("config.avoidBbwShotWithoutScale", bool(noScaleBbwEnabled(config.noScaleBbwMode)));
  delta.field("config.noScaleBbwMode", "\"%s\"", noScaleBbwModeId(config.noScaleBbwMode));
  delta.field("config.scaleMacCacheMode", "\"%s\"", scaleMacCacheModeId(config.scaleMacCacheMode));
  delta.field("config.rinseEnabled", bool(config.rinseEnabled));
  delta.field("state", "\"%s\"", stopperStateName(control.state));
  delta.field("stateLabel", "\"%s\"", stateLabel(control.state));
  delta.field("machineState", "\"%s\"", machineRunStateName(control.machineRunState));
  delta.field("machineRunning", bool(control.machineRunning));
  delta.field("physicalActivatorOn", bool(control.physicalActivatorOn));
  delta.field("remoteControlEnabled", bool(control.remoteControlEnabled));
  delta.field("safety.state", "\"%s\"", relaySafetyStateName(control.safetyState));
  delta.field("safety.recoveryRequired", bool(control.resetRecoveryRequired));
  delta.field("backflush.waiting", bool(control.backflushWaiting));
  delta.field("backflush.ready", bool(control.backflushReady));
  delta.field("backflush.unresolved", bool(control.backflushUnresolved));
  delta.field("backflush.stopReason", "\"%s\"", control.backflushStopReason);
  delta.field("scale.available", bool(control.scaleAvailable));
  delta.field("scale.streamState", "\"%s\"", weightStreamStateName(control.weightStreamState));
  // Disconnected readings are never inspected. One null transition clears UI.
  if (control.scaleAvailable && control.observedWeightValid && std::isfinite(control.observedWeightG))
    delta.field("scale.observedWeightG", control.observedWeightG);
  else if (control.scaleAvailable && control.currentWeightValid && std::isfinite(control.currentWeightG))
    delta.field("scale.observedWeightG", control.currentWeightG);
  else delta.field("scale.observedWeightG", "null");
  if (control.scaleAvailable && control.currentTimerValid)
    delta.field("scale.timerMs", "%lu", static_cast<unsigned long>(control.currentTimerMs / 100 * 100));
  else delta.field("scale.timerMs", "null");
  if (callbacks_.copyScaleHistory) callbacks_.copyScaleHistory(g_work->scaleHistory);
  else memset(&g_work->scaleHistory, 0, sizeof(g_work->scaleHistory));
  char friendly[PREFERRED_SCALE_NAME_CAPACITY] = {};
  findScaleHistoryFriendlyName(g_work->scaleHistory, control.preferredScaleMac, friendly, sizeof(friendly));
  escapeJsonString(friendly, g_work->historyJson, NetworkWorkBuf::kHistoryJson);
  delta.field("scale.preferredFriendlyName", "\"%s\"", g_work->historyJson);
  delta.field("scale.preferredMac", "\"%s\"", control.preferredScaleMac);
  escapeJsonString(control.preferredScaleName, g_work->historyJson, NetworkWorkBuf::kHistoryJson);
  delta.field("scale.preferredName", "\"%s\"", g_work->historyJson);
  delta.field("scale.protocol", "\"%s\"", control.scaleProtocol);
  // Whole-second ceiling (UI resolution): the delta hash must change only
  // when the displayed countdown does, not on every stream tick.
  delta.field("scale.macCachePauseRemainingMs", "%lu",
              static_cast<unsigned long>((control.scaleMacCachePauseRemainingMs + 999U) / 1000U * 1000U));
  delta.field("scale.lastDisconnectReasonName", "\"%s\"", scaleDisconnectReasonName(control.scaleLastDisconnectReason));
  delta.field("connections.wifiConnected", bool(network.staState == StaState::CONNECTED));
  delta.field("connections.wifiLevel", "%s", connectionSignalLevelJson(
      network.staLinkMetricsValid, network.staRssi));
  delta.field("connections.bluetoothConnected", bool(control.scaleAvailable));
  delta.field("connections.bluetoothLevel", "%s", connectionSignalLevelJson(
      control.scaleRssiValid, control.scaleRssi));
  escapeJsonString(network.staState == StaState::CONNECTED ? network.staSsid : "", g_work->historyJson, NetworkWorkBuf::kHistoryJson);
  delta.field("connections.wifiName", "\"%s\"", g_work->historyJson);
  escapeConnectedScaleName(control, g_work->scaleHistory, g_work->historyJson, NetworkWorkBuf::kHistoryJson);
  delta.field("connections.bluetoothName", "\"%s\"", g_work->historyJson);
  const auto &tare = control.cupTare;
  const uint8_t idleCode = idleTarePresentationCode(tare, control.cupPresent);
  delta.field("cupPresence.state", "\"%s\"", control.scaleAvailable
      ? cupPresenceStateName(control.cupPresenceState) : "UNKNOWN");
  delta.field("cupPresence.present", bool(control.cupPresent));
  if (control.cupPresent && tare.weightValid && std::isfinite(tare.weightG))
    delta.field("cupPresence.weightG", tare.weightG);
  else delta.field("cupPresence.weightG", "null");
  delta.field("cupPresence.weightValid", bool(control.cupPresent && tare.weightValid && std::isfinite(tare.weightG)));
  delta.field("cupPresence.idleTare", "\"%s\"", idleCode == IDLE_WAITING_FOR_SETTLE ? "empty" :
      idleCode == IDLE_READY_FOR_CUP ? "ready" : idleTarePresentationName(idleCode));
  delta.field("cycle.active", bool(control.activeCycle));
  delta.field("cycle.shotType", "\"%s\"", activeCycleShotTypeLabel(control));
  delta.field("cycle.extractionExtended", bool(control.cycleExtractionExtended));
  delta.field("cycle.slowExtractionExtended", bool(control.cycleSlowExtractionExtended));
  delta.field("cycle.activeStopWeightG", control.cycleActiveStopWeightG);
  delta.field("cycle.minBbwBrewTimeRemainingMs", "%lu", static_cast<unsigned long>(control.cycleMinBbwBrewTimeRemainingMs));
  delta.field("cycle.autoToManualGuardArmed", bool(control.cycleAutoToManualGuardArmed));
  delta.field("cycle.autoToManualGuardEnforced", bool(control.cycleAutoToManualGuardEnforced));
  delta.field("cycle.autoToManualGuardRemainingMs", "%lu", static_cast<unsigned long>(control.cycleAutoToManualGuardRemainingMs));
  delta.field("cycle.accidentalTouchHolding", bool(control.cycleAccidentalTouchHolding));
  delta.field("lastShot.valid", bool(latest));
  delta.field("lastShot.endReason", "\"%s\"", latest ? shotLogStopDetailName(static_cast<ShotLogStopDetail>(latest->stopDetail)) : "none");
  delta.field("noScaleShotGuard.enabled", bool(control.noScaleShotGuardEnabled));
  delta.field("noScaleShotGuard.armed", bool(control.noScaleShotGuardArmed));
  delta.field("noScaleShotGuard.mode", "\"%s\"", noScaleBbwModeId(config.noScaleBbwMode));
  // Whole-minute ceiling (UI resolution): an hour-long cooldown must not wake
  // the idle stream on every tick.
  delta.field("noScaleShotGuard.cooldownRemainingMs", "%lu",
              static_cast<unsigned long>((control.noScaleShotGuardCooldownRemainingMs + 59999U) / 60000U * 60000U));
  delta.field("noScaleShotGuard.scaleUsable", bool(control.scaleAvailable && control.weightStreamState == WeightStreamState::FRESH));
  if (callbacks_.copyPresetBank) callbacks_.copyPresetBank(&g_work->presetBank);
  else memset(&g_work->presetBank, 0, sizeof(g_work->presetBank));
  buildSlimPresetsJson(g_work->presetBank);
  delta.field("presets", "%s", g_work->presetsJson);
#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD
  const auto micra = machineIntegrationStatus();
  const auto ws = machineIntegrationWebSocketStatus();
  uint8_t options, connection;
  { TaskLockGuard lock(dataMux_); options = settings_.lineaMicra.options; connection = settings_.lineaMicra.connectionType; }
  delta.field("machineIntegration", "\"linea_micra_cloud\"");
  delta.field("lineaMicra.powerState", "\"%s\"", lineaMicraPowerStateName(micra.powerState));
  delta.field("lineaMicra.quality", "\"%s\"", lineaMicraObservationQualityName(micra.quality));
  delta.field("lineaMicra.optimisticOn", bool(micra.optimisticOn));
  delta.field("lineaMicra.optimisticOff", bool(micra.optimisticOff));
  delta.field("lineaMicra.accountConfigured", bool(micra.accountConfigured));
  delta.field("lineaMicra.observeState", bool(options & LINEA_MICRA_OBSERVE_STATE));
  delta.field("lineaMicra.connectionType", "\"%s\"", micraConnectionTypeName(connection));
  delta.field("lineaMicra.websocket.cleaningAvailable", bool(ws.cleaningAvailable));
  delta.field("lineaMicra.websocket.cleaning", "\"%s\"", ws.cleaning == MicraCleaningState::OFF ? "inactive" :
      ws.cleaning == MicraCleaningState::REQUESTED ? "waiting_for_paddle" :
      ws.cleaning == MicraCleaningState::CLEANING ? "cleaning" : "unknown");
  escapeJsonString(ws.cleaningLabel, g_work->historyJson, NetworkWorkBuf::kHistoryJson);
  delta.field("lineaMicra.websocket.cleaningLabel", "\"%s\"", g_work->historyJson);
#else
  delta.field("machineIntegration", "\"none\"");
#endif
  if (!delta.ok || !statusJsonAppend(&delta.used, "}}")) return false;
  if (!delta.changed) return true;
  bool owner;
  { TaskLockGuard lock(dataMux_); owner = strcmp(session.clientId, activeWebUiClientId_) == 0; }
  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = reinterpret_cast<uint8_t *>(g_work->statusJson);
  frame.len = delta.used;
  if (!owner) return false;
  if (httpd_ws_send_frame_async(server_, session.fd, &frame) != ESP_OK) {
    // The delta hashes were already consumed for the dropped frame, so only
    // a full snapshot can repair the client; defer on a clean header abort.
    if (!shotStreamSendCleanAbort) return false;
    session.homeResync = true;
    return true;
  }
  session.homeBoot = control.bootId;
  session.homeResync = false;
  return true;
}

// Same bounded projection contract as sendHomeStream: the diagnostic page
// subscribes per view, so a session without the flag never reaches this path.
__attribute__((optimize("Os")))
bool ShotStopperNetwork::sendDiagnosticStream(ShotStreamSession &session,
                                              const ControlStatusSnapshot &control) {
  const bool initial = session.diagResync || session.diagBoot != control.bootId;
  HomeStreamDelta delta{session.diagHashes, ShotStreamSession::kDiagFields, 0, 0, 0, initial};
  delta.ok = statusJsonAppend(&delta.used,
      "{\"v\":1,\"type\":\"diagnostic\",\"boot\":%lu,\"snapshot\":%s,\"changes\":{",
      static_cast<unsigned long>(control.bootId),
      initial ? "true" : "false");
  delta.field("state", "\"%s\"", stopperStateName(control.state));
  delta.field("machineState", "\"%s\"", machineRunStateName(control.machineRunState));
  delta.field("backflush.waiting", bool(control.backflushWaiting));
  delta.field("backflush.ready", bool(control.backflushReady));
  delta.field("backflush.unresolved", bool(control.backflushUnresolved));
  delta.field("backflush.stopReason", "\"%s\"", control.backflushStopReason);
  delta.field("backflush.hardLimitMs", "%lu", static_cast<unsigned long>(control.safetyHardLimitMs));
  delta.field("backflush.remainingMs", "%lu", static_cast<unsigned long>(control.safetyRemainingMs));
  delta.field("cupPresence.state", "\"%s\"", control.scaleAvailable
      ? cupPresenceStateName(control.cupPresenceState) : "UNKNOWN");
  delta.field("cupPresence.present", bool(control.cupPresent));
  const bool cupWeightValid = control.cupPresent && control.cupTare.weightValid &&
      std::isfinite(control.cupTare.weightG);
  if (cupWeightValid)
    delta.field("cupPresence.weightG", control.cupTare.weightG);
  else delta.field("cupPresence.weightG", "null");
  delta.field("cupPresence.weightValid", bool(cupWeightValid));
  delta.field("physicalActivatorOn", bool(control.physicalActivatorOn));
  delta.field("reedOn", bool(control.reedOn));
  delta.field("relayClosed", bool(control.relayClosed));
  delta.field("controlSource", "\"%s\"", controlSourceName(control.source));
  delta.field("safety.state", "\"%s\"", relaySafetyStateName(control.safetyState));
  delta.field("safety.fault", "\"%s\"", relaySafetyFaultName(control.safetyFault));
  delta.field("safety.taskWatchdogReady", bool(control.taskWatchdogReady));
  delta.field("safety.externalHardware", bool(control.externalSafetyPresent));
  delta.field("safety.recoveryRequired", bool(control.resetRecoveryRequired));
  delta.field("scale.available", bool(control.scaleAvailable));
  delta.field("scale.streamState", "\"%s\"", weightStreamStateName(control.weightStreamState));
  delta.field("scale.controlState", "\"%s\"", weightControlStateName(control.weightControlState));
  if (control.scaleRssiValid)
    delta.field("scale.rssi", "%d", static_cast<int>(control.scaleRssi));
  else delta.field("scale.rssi", "null");
  if (control.weightStreamState == WeightStreamState::FRESH &&
      control.scaleWeightUpdateIntervalMs != 0)
    delta.field("scale.weightUpdateIntervalMs", "%lu",
                static_cast<unsigned long>(control.scaleWeightUpdateIntervalMs));
  else delta.field("scale.weightUpdateIntervalMs", "null");
  if (control.scaleAvailable && control.currentTimerValid)
    delta.field("scale.timerMs", "%lu",
                static_cast<unsigned long>(control.currentTimerMs / 100 * 100));
  else delta.field("scale.timerMs", "null");
  // Same observed-then-current preference and null gating as the Home stream.
  if (control.scaleAvailable && control.observedWeightValid && std::isfinite(control.observedWeightG))
    delta.field("scale.observedWeightG", control.observedWeightG);
  else if (control.scaleAvailable && control.currentWeightValid && std::isfinite(control.currentWeightG))
    delta.field("scale.observedWeightG", control.currentWeightG);
  else delta.field("scale.observedWeightG", "null");
  // sendHomeStream filled g_work->scaleHistory earlier in this same dispatch.
  char connectedFriendly[PREFERRED_SCALE_NAME_CAPACITY] = {};
  findScaleHistoryFriendlyName(g_work->scaleHistory, control.connectedScaleMac,
                               connectedFriendly, sizeof(connectedFriendly));
  char safeConnectedMac[PREFERRED_SCALE_MAC_CAPACITY * 2] = {};
  char safeConnectedFriendly[PREFERRED_SCALE_NAME_CAPACITY * 2] = {};
  sanitizeJsonEmbed(control.connectedScaleMac, safeConnectedMac,
                    sizeof(safeConnectedMac));
  sanitizeJsonEmbed(connectedFriendly, safeConnectedFriendly,
                    sizeof(safeConnectedFriendly));
  delta.field("scale.connectedMac", "\"%s\"", safeConnectedMac);
  delta.field("scale.connectedFriendlyName", "\"%s\"", safeConnectedFriendly);
  delta.field("scale.packetGaps", "%lu", static_cast<unsigned long>(control.scalePacketGaps));
  delta.field("scale.maxPacketGapMs", "%lu", static_cast<unsigned long>(control.scaleMaxPacketGapMs));
  delta.field("scale.rejectedPackets", "%lu", static_cast<unsigned long>(control.scaleRejectedPackets));
  delta.field("scale.reconnects", "%lu", static_cast<unsigned long>(control.scaleReconnects));
  delta.field("scale.eventsDropped", "%lu", static_cast<unsigned long>(control.scaleEventsDropped));
  delta.field("scale.recoveredStaleCount", "%lu", static_cast<unsigned long>(control.scaleRecoveredStaleCount));
  delta.field("scale.recoveredStaleMs", "%lu", static_cast<unsigned long>(control.scaleRecoveredStaleMs));
  delta.field("scale.lastDisconnectReasonName", "\"%s\"",
              scaleDisconnectReasonName(control.scaleLastDisconnectReason));
  const ScaleBleDiagnostics &ble = control.scaleBleDiagnostics;
  char disconnectSummary[144] = {}, commandSummary[64] = {};
  if (ble.disconnectSequence != 0) {
    snprintf(disconnectSummary, sizeof(disconnectSummary), "%s · %s · %s %ld · %lus ago",
             scaleDisconnectReasonName(ble.disconnectReason),
             scaleBleOriginName(ble.disconnectOrigin),
             scaleBleStatusDomain(ble.disconnectStatus, ble.disconnectOrigin),
             static_cast<long>(ble.disconnectStatus),
             static_cast<unsigned long>(static_cast<uint32_t>(millis() - ble.disconnectAtMs) / 1000));
  }
  if (ble.commandFailureSequence != 0) {
    snprintf(commandSummary, sizeof(commandSummary), "%s · status %ld · %lums",
             scaleBleCommandName(ble.command), static_cast<long>(ble.commandStatus),
             static_cast<unsigned long>(ble.commandElapsedMs));
  }
  delta.field("scale.lastDisconnect.summary", "\"%s\"", disconnectSummary);
  delta.field("scale.lastCommandFailure.summary", "\"%s\"", commandSummary);
#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD
  {
    const auto micra = machineIntegrationStatus();
    uint8_t options, connection;
    { TaskLockGuard lock(dataMux_); options = settings_.lineaMicra.options; connection = settings_.lineaMicra.connectionType; }
    const bool sampleValid = micra.sampleAtMs != 0 &&
        (micra.quality == LineaMicraObservationQuality::CURRENT ||
         micra.quality == LineaMicraObservationQuality::STALE ||
         micra.quality == LineaMicraObservationQuality::UNSUPPORTED);
    delta.field("lineaMicra.accountConfigured", bool(micra.accountConfigured));
    delta.field("lineaMicra.observeState", bool(options & LINEA_MICRA_OBSERVE_STATE));
    delta.field("lineaMicra.connectionType", "\"%s\"", micraConnectionTypeName(connection));
    delta.field("lineaMicra.powerState", "\"%s\"", lineaMicraPowerStateName(micra.powerState));
    delta.field("lineaMicra.observedMode", "\"%s\"", lineaMicraObservedModeName(micra.observedMode));
    delta.field("lineaMicra.quality", "\"%s\"", lineaMicraObservationQualityName(micra.quality));
    delta.field("lineaMicra.sampleValid", bool(sampleValid));
    // Whole seconds: the UI renders integer age and interpolates locally, so
    // a raw millisecond age would force one frame every dispatch tick.
    delta.field("lineaMicra.sampleAgeMs", "%lu",
                static_cast<unsigned long>(micra.sampleAtMs == 0 ? 0 : static_cast<uint32_t>(millis() - micra.sampleAtMs) / 1000 * 1000));
    delta.field("lineaMicra.freshnessPolicy", "\"%s\"",
                micra.connectionFreshness ? "connection" : "time");
    delta.field("lineaMicra.freshnessMs", "%lu",
                static_cast<unsigned long>(micra_timing::kStateFreshnessMs));
    delta.field("lineaMicra.powerSource", "\"%s\"", micraPowerSourceName(micra.powerSource));
    delta.field("lineaMicra.phase", "\"%s\"", lineaMicraPhaseName(micra.phase));
    delta.field("lineaMicra.temperatureState", "\"%s\"", lineaMicraTemperatureStateName(micra.temperatureState));
    delta.field("lineaMicra.temperatureError", "\"%s\"", lineaMicraErrorName(micra.temperatureError));
  }
#endif
  if (!delta.ok || !statusJsonAppend(&delta.used, "}}")) return false;
  if (!delta.changed) return true;
  bool owner;
  { TaskLockGuard lock(dataMux_); owner = strcmp(session.clientId, activeWebUiClientId_) == 0; }
  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = reinterpret_cast<uint8_t *>(g_work->statusJson);
  frame.len = delta.used;
  if (!owner) return false;
  if (httpd_ws_send_frame_async(server_, session.fd, &frame) != ESP_OK) {
    // Same repair rule as the Home stream: defer with a snapshot armed.
    if (!shotStreamSendCleanAbort) return false;
    session.diagResync = true;
    return true;
  }
  session.diagBoot = control.bootId;
  session.diagResync = false;
  return true;
}

// Same bounded, view-scoped contract as the diagnostic sender: only a session
// holding the History subscription reaches this path. A cheap store epoch
// gates the page copy, and a component fingerprint (epoch, window, paging,
// and the raw record bytes) suppresses the send while the page is unchanged —
// the HomeStreamDelta no-change contract. A pending one-shot fetch rides the
// same frame but never disturbs the standing window's bookkeeping.
__attribute__((optimize("Os")))
bool ShotStopperNetwork::sendHistoryStream(ShotStreamSession &session,
                                           const ControlStatusSnapshot &control) {
  const uint32_t epoch = callbacks_.historyEpoch ? callbacks_.historyEpoch() : 0;
  const bool initial =
      session.historyResync || session.historyBoot != control.bootId;
  // A snapshot supersedes any pending append fetch: the client resets its
  // in-flight marker when a replace lands.
  if (initial) session.historyFetch = false;
  const bool fetch = session.historyFetch;
  if (!initial && !fetch && epoch == session.historyEpoch) return true;
  const size_t offset =
      fetch ? session.historyFetchOffset : session.historyOffset;
  // The page is copied under the store mutex by the bridge callback, so a
  // torn multi-word record is never observed. The union member is exclusive
  // to this dispatch; the shot-card tail rebuilds jsonItem only afterwards.
  HistoryPage &page = g_work->historyPage;
  if (callbacks_.copyHistoryPage) {
    callbacks_.copyHistoryPage(page, offset, session.historyLimit,
                               session.historyDir);
  } else {
    page.total = page.start = page.count = 0;
    page.hasMore = false;
  }
  // Component fingerprint (the HomeStreamDelta no-change contract): the raw
  // packed record bytes plus the page/window identity. HistoryRecord is a
  // packed 16-byte POD and time-varying snapshot fields (ui) stay outside,
  // so the value is comparable and storable on snapshot sends too. Worst
  // frame size: 120 maximal records plus the ui block measure ~20.8 KiB,
  // under the client's 24 KiB frame guard.
  uint32_t fingerprint = crc32Update(0xFFFFFFFFU,
      reinterpret_cast<const uint8_t *>(&epoch), sizeof(epoch));
  const uint32_t window[3] = {static_cast<uint32_t>(page.start),
                              static_cast<uint32_t>(session.historyLimit),
                              static_cast<uint32_t>(session.historyDir)};
  fingerprint = crc32Update(fingerprint,
      reinterpret_cast<const uint8_t *>(window), sizeof(window));
  const uint32_t paging[2] = {static_cast<uint32_t>(page.total),
                              page.hasMore ? 1u : 0u};
  fingerprint = crc32Update(fingerprint,
      reinterpret_cast<const uint8_t *>(paging), sizeof(paging));
  if (page.count)
    fingerprint = crc32Update(fingerprint,
        reinterpret_cast<const uint8_t *>(page.records),
        page.count * sizeof(HistoryRecord));
  if (!initial && !fetch && fingerprint == session.historyFingerprint) {
    // Content identical (e.g. a no-op rating bump): adopt the moved epoch so
    // idle ticks stop at the cheap gate instead of rebuilding the page.
    session.historyEpoch = epoch;
    return true;
  }
  bool override = false;
  {
    TaskLockGuard lock(dataMux_);
    override = webUiOverrideActive_ &&
        static_cast<int32_t>(millis() - webUiOverrideUntilMs_) < 0;
  }
  size_t used = 0;
  if (!statusJsonAppend(&used,
          "{\"v\":1,\"type\":\"history\",\"boot\":%lu,\"snapshot\":%s,"
          "\"epoch\":%lu,",
          static_cast<unsigned long>(control.bootId),
          initial ? "true" : "false",
          static_cast<unsigned long>(epoch)) ||
      (initial && (!appendRecordPageUi(control, &used, override) ||
                   !statusJsonAppend(&used, ","))) ||
      !statusJsonAppend(&used,
          "\"bootId\":%lu,\"total\":%u,\"offset\":%u,\"limit\":%u,"
          "\"hasMore\":%s,\"records\":[",
          static_cast<unsigned long>(control.bootId),
          static_cast<unsigned>(page.total), static_cast<unsigned>(page.start),
          static_cast<unsigned>(session.historyLimit),
          page.hasMore ? "true" : "false"))
    return false;
  for (size_t index = 0; index < page.count; ++index) {
    const HistoryRecord &record = page.records[index];
    if (!statusJsonAppend(&used,
            "%s{\"id\":%lu,\"type\":\"%s\",\"durationS\":%.1f,"
            "\"hasWeight\":%s,\"hasWallTime\":%s,\"endedAtUnixSec\":%lu,"
            "\"endedAtLocalSec\":%lu}",
            index == 0 ? "" : ",",
            static_cast<unsigned long>(record.id),
            historyTypeName(static_cast<HistoryType>(record.type)),
            static_cast<double>(record.durationDs) / 10.0,
            (record.flags & HISTORY_FLAG_NO_WEIGHT) == 0 ? "true" : "false",
            (record.flags & HISTORY_FLAG_WALL_TIME) != 0 ? "true" : "false",
            static_cast<unsigned long>(record.endedAtUnixSec),
            static_cast<unsigned long>(record.endedAtLocalSec)))
      return false;
  }
  if (!statusJsonAppend(&used, "]}")) return false;
  // Recheck ownership immediately before delivery; telemetry never renews it.
  bool owner;
  { TaskLockGuard lock(dataMux_); owner = strcmp(session.clientId, activeWebUiClientId_) == 0; }
  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = reinterpret_cast<uint8_t *>(g_work->statusJson);
  frame.len = used;
  if (!owner) return false;
  if (httpd_ws_send_frame_async(server_, session.fd, &frame) != ESP_OK) {
    // Nothing was delivered: the snapshot/fetch/epoch state that produced
    // this frame is still armed, so the next dispatch retries it unchanged.
    return shotStreamSendCleanAbort;
  }
  session.historyBoot = control.bootId;
  session.historyResync = false;
  session.historyFetch = false;
  if (!fetch) {
    session.historyEpoch = epoch;
    session.historyFingerprint = fingerprint;
  }
  return true;
}

// The Stats row projection (extracted verbatim from the REST shots handler):
// row scalars plus the curve fields embedded at the top level, because
// formatShotCurveJsonBody emits the curve object's inner fields. Writes into
// work.jsonItem using work.curveJson and work.serializedCurve as scratch and
// returns the written length, 0 on overflow.
size_t ShotStopperNetwork::formatShotStatsRow(NetworkWorkBuf &work,
                                              const ShotLogRecord &record,
                                              const ShotCurveRecord *curves,
                                              size_t curveCount) {
  char actual[16] = "null";
  char errorG[16] = "null";
  char errorPct[16] = "null";
  char flow[16] = "null";
  char firstDrop[16] = "null";
  char tareAt[16] = "null";
  char maxRecovery[16] = "null";
  char minBbwBrewTime[16] = "null";
  char targetEarly[16] = "null";
  char presetName[SHOT_PRESET_NAME_CAPACITY * 6] = {};
  char scaleNameJson[SCALE_FRIENDLY_NAME_MAX_LEN * 6 + 1] = {};
  escapeJsonString(record.presetName, presetName, sizeof(presetName));
  escapeJsonString(record.scaleName, scaleNameJson, sizeof(scaleNameJson));
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
  if (record.tareAtDs != SHOT_LOG_METRIC_MISSING) {
    snprintf(tareAt, sizeof(tareAt), "%.1f",
             static_cast<double>(record.tareAtDs) / 10.0);
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
  ShotCurveRecord &emptyCurve = work.serializedCurve;
  resetShotCurveRecord(emptyCurve);
  const ShotCurveRecord *curve =
      findShotCurveById(curves, curveCount, record.id);
  if (curve == nullptr) {
    curve = &emptyCurve;
  }
  char *const curveJson = work.curveJson;
  char alphaJson[5] = "null";
  const unsigned alpha = shotLogBbwAlpha(record);
  if (alpha) snprintf(alphaJson, sizeof(alphaJson), "%u.%02u", alpha / 100, alpha % 100);
  if (!formatShotCurveJsonBody(curveJson, SHOT_CURVE_JSON_CAPACITY, *curve) &&
      !formatShotCurveJsonBody(curveJson, SHOT_CURVE_JSON_CAPACITY,
                               emptyCurve)) {
    // Keep the row JSON parseable; an empty curve beats a broken page.
    curveJson[0] = '\0';
  }
  const int bytes = snprintf(work.jsonItem, NetworkWorkBuf::kJsonItem,
      "{\"id\":%lu,\"bootId\":%lu,\"endedAtMs\":%lu,"
      "\"hasWallTime\":%s,\"endedAtLocalSec\":%lu,"
      "\"endedAtUnixSec\":%lu,\"timezoneOffsetMinutesAtCommit\":%d,"
      "\"durationS\":%.1f,\"presetId\":%u,\"presetName\":\"%s\","
      "\"scaleName\":\"%s\","
      "\"goalG\":%u,\"actualG\":%s,\"errorG\":%s,\"errorPct\":%s,"
      "\"offsetG\":%.2f,\"bbwAlgorithm\":\"%s\",\"bbwAlgorithmVersion\":%s,"
      "\"bbwAlpha\":%s,\"bbwLearningApplied\":%s,\"avgFlowGS\":%s,"
      "\"firstDropS\":%s,\"tareS\":%s,"
      "\"shotType\":\"%s\",\"cutType\":\"%s\","
      "\"extractionGuardEnabled\":%s,\"extractionExtended\":%s,"
      "\"slowExtractionGuardEnabled\":%s,\"slowExtractionExtended\":%s,"
      "\"stopDetail\":\"%s\",\"maxRecoveryWeightG\":%s,"
      "\"minBbwBrewTimeS\":%s,\"targetReachedEarlyS\":%s,"
      "\"actualWeightSource\":\"%s\",\"rating\":%u,%s}",
      static_cast<unsigned long>(record.id),
      static_cast<unsigned long>(record.bootId),
      static_cast<unsigned long>(record.endedAtMs),
      record.hasWallTime ? "true" : "false",
      static_cast<unsigned long>(record.endedAtLocalSec),
      static_cast<unsigned long>(record.endedAtUnixSec),
      static_cast<int>(record.timezoneOffsetMinutesAtCommit),
      static_cast<double>(record.durationDs) / 10.0,
      static_cast<unsigned>(shotLogPresetId(record)),
      presetName, scaleNameJson,
      static_cast<unsigned>(record.goalWeightG), actual, errorG, errorPct,
      static_cast<double>(record.offsetUsedCg) / 100.0,
      shotLogBbwAlgorithm(record), shotLogBbwVersion(record),
      alphaJson, shotLogBbwLearningApplied(record), flow, firstDrop, tareAt,
      shotLogTypeName(shotLogType(record)),
      shotLogCutName(shotLogCut(record)),
      shotLogFastGuardEnabled(record.extractionGuardEnabled) ? "true" : "false",
      shotLogFastExtended(record.extractionExtended) ? "true" : "false",
      shotLogSlowGuardEnabled(record.extractionGuardEnabled) ? "true" : "false",
      shotLogSlowExtended(record.extractionExtended) ? "true" : "false",
      shotLogStopDetailName(static_cast<ShotLogStopDetail>(record.stopDetail)),
      maxRecovery, minBbwBrewTime, targetEarly,
      actualWeightSourceName(static_cast<ActualWeightSource>(record.actualWeightSource)),
      static_cast<unsigned>(shotLogRating(record.extractionGuardEnabled)),
      curveJson);
  if (bytes <= 0 || static_cast<size_t>(bytes) >= NetworkWorkBuf::kJsonItem)
    return 0;
  return static_cast<size_t>(bytes);
}

// Same bounded, view-scoped contract as the history sender, plus frame
// budgeting and pacing: full curves make a page larger than one frame, so a
// dispatch delivers one budgeted frame and the page continues across
// dispatch ticks (rowBase/more) until a terminating frame. The cheap store
// epoch gates the record/curve copy, and the fingerprint — epoch, window,
// aggregate, and per-row id/packed-rating/curve hashes, since records are
// immutable after commit except the packed rating byte — suppresses the
// send while the page is unchanged.
__attribute__((optimize("Os")))
bool ShotStopperNetwork::sendStatsStream(ShotStreamSession &session,
                                         const ControlStatusSnapshot &control) {
  const uint32_t epoch =
      callbacks_.shotLogEpoch ? callbacks_.shotLogEpoch() : 0;
  const bool initial =
      session.statsResync || session.statsBoot != control.bootId;
  // A snapshot supersedes any pending fetch: the client drops its buffered
  // partial page and in-flight markers when a replace lands.
  if (initial) session.statsFetch = false;
  const bool fetch = session.statsFetch;
  // The in-flight page survives only unchanged: a fetch or a store change
  // supersedes it, and a fetch preempting the standing page re-arms the
  // standing snapshot so the view is not left on pre-fetch rows.
  if (session.statsPaging && (fetch != session.statsPageFetch ||
                              session.statsPageEpoch != epoch || initial)) {
    // A fetch preempting the standing page re-arms the standing snapshot
    // only when the fetch completes, so the fetch's own continuation
    // dispatches never look initial.
    if (fetch && !session.statsPageFetch) session.statsRedeliverStanding = true;
    session.statsPaging = false;
  }
  if (!session.statsPaging && !initial && !fetch && epoch == session.statsEpoch) return true;
  const size_t offset = fetch ? session.statsFetchOffset : session.statsOffset;
  const size_t limit = fetch ? session.statsFetchLimit : session.statsLimit;
  const ShotLogSort sort = fetch ? session.statsFetchSort : session.statsSort;
  const ShotLogSortDir dir = fetch ? session.statsFetchDir : session.statsDir;
  NetworkWorkBuf &work = *g_work;
  const ShotStatsView stats =
      callbacks_.copyShotStats != nullptr ? callbacks_.copyShotStats()
                                          : ShotStatsView{};
  const size_t count =
      callbacks_.copyShotRecords != nullptr
          ? callbacks_.copyShotRecords(work.shotRecords, SHOT_LOG_CAPACITY)
          : 0;
  const size_t curveCount =
      callbacks_.copyShotCurves != nullptr
          ? callbacks_.copyShotCurves(work.shotCurves, SHOT_CURVE_CAPACITY)
          : 0;
  shotLogSortRecords(work.shotRecords, count, sort, dir);
  size_t start = 0, pageCount = 0;
  const bool hasMore =
      shotLogPageSlice(count, offset, limit, start, pageCount);
  char statsHeader[384] = {};
  const bool statsOk =
      buildIntegrationStats(stats, statsHeader, sizeof(statsHeader));
  uint32_t fingerprint = crc32Update(0xFFFFFFFFU,
      reinterpret_cast<const uint8_t *>(&epoch), sizeof(epoch));
  const uint32_t window[4] = {static_cast<uint32_t>(offset),
                              static_cast<uint32_t>(limit),
                              static_cast<uint32_t>(sort),
                              static_cast<uint32_t>(dir)};
  fingerprint = crc32Update(fingerprint,
      reinterpret_cast<const uint8_t *>(window), sizeof(window));
  if (statsOk)
    fingerprint = crc32Update(fingerprint,
        reinterpret_cast<const uint8_t *>(statsHeader), strlen(statsHeader));
  resetShotCurveRecord(work.serializedCurve);
  for (size_t index = start; index < start + pageCount; ++index) {
    const ShotLogRecord &record = work.shotRecords[index];
    const ShotCurveRecord *curve =
        findShotCurveById(work.shotCurves, curveCount, record.id);
    const uint32_t rowHash =
        shotStreamCurveHash(curve ? *curve : work.serializedCurve,
                            curve ? curve->count : 0);
    const uint32_t row[2] = {record.id, record.extractionGuardEnabled};
    fingerprint = crc32Update(fingerprint,
        reinterpret_cast<const uint8_t *>(row), sizeof(row));
    fingerprint = crc32Update(fingerprint,
        reinterpret_cast<const uint8_t *>(&rowHash), sizeof(rowHash));
  }
  if (!session.statsPaging && !initial && !fetch && fingerprint == session.statsFingerprint) {
    // Content identical (e.g. a no-op rating bump): adopt the moved epoch so
    // idle ticks stop at the cheap gate instead of recopying the records.
    session.statsEpoch = epoch;
    return true;
  }
  bool override = false;
  {
    TaskLockGuard lock(dataMux_);
    override = webUiOverrideActive_ &&
        static_cast<int32_t>(millis() - webUiOverrideUntilMs_) < 0;
  }
  if (!session.statsPaging) {
    // Page start: identity is captured once and the resync/boot markers move
    // here from completion, so continuation dispatches are not initial.
    ++session.statsSeq;
    session.statsPaging = true;
    session.statsSent = 0;
    session.statsPageEpoch = epoch;
    session.statsPageSnapshot = initial;
    session.statsPageFetch = fetch;
    session.statsBoot = control.bootId;
    session.statsResync = false;
  }
  const bool first = session.statsSent == 0;
  const size_t sentBase = session.statsSent;
  size_t used = 0;
  if (!statusJsonAppend(&used,
          "{\"v\":1,\"type\":\"stats\",\"boot\":%lu,\"snapshot\":%s,"
          "\"epoch\":%lu,\"seq\":%lu,",
          static_cast<unsigned long>(control.bootId),
          first && session.statsPageSnapshot ? "true" : "false",
          static_cast<unsigned long>(epoch),
          static_cast<unsigned long>(session.statsSeq)))
      return false;
  if (first && session.statsPageSnapshot &&
      (!appendRecordPageUi(control, &used, override) ||
       !statusJsonAppend(&used, ",")))
    return false;
  if (first && statsOk &&
      !statusJsonAppend(&used, "\"stats\":%s,", statsHeader))
    return false;
  if (!statusJsonAppend(&used,
          "\"bootId\":%lu,\"total\":%u,\"offset\":%u,\"limit\":%u,"
          "\"hasMore\":%s,\"rows\":[",
          static_cast<unsigned long>(control.bootId),
          static_cast<unsigned>(count), static_cast<unsigned>(start),
          static_cast<unsigned>(limit), hasMore ? "true" : "false"))
    return false;
  const size_t rowsBase = used;
  while (session.statsSent < pageCount) {
    const size_t rowBytes = formatShotStatsRow(
        work, work.shotRecords[start + session.statsSent], work.shotCurves,
        curveCount);
    if (!rowBytes) return false;
    // Close the frame before this row unless the frame is empty; a single
    // oversized row still fits under the client's frame guard alone.
    if (used + (used > rowsBase ? 1u : 0u) + rowBytes + 48u >
            kStatsStreamFrameBytes &&
        used > rowsBase)
      break;
    if (!statusJsonAppend(&used, "%s%s", used > rowsBase ? "," : "",
                          work.jsonItem))
      return false;
    ++session.statsSent;
  }
  const bool more = session.statsSent < pageCount;
  if (!statusJsonAppend(&used, "],\"rowBase\":%u,\"more\":%s}",
          static_cast<unsigned>(sentBase), more ? "true" : "false"))
    return false;
  // Recheck ownership immediately before every delivery.
  bool owner;
  { TaskLockGuard lock(dataMux_); owner = strcmp(session.clientId, activeWebUiClientId_) == 0; }
  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = reinterpret_cast<uint8_t *>(g_work->statusJson);
  frame.len = used;
  if (!owner) return false;
  if (httpd_ws_send_frame_async(server_, session.fd, &frame) != ESP_OK) {
    // A clean header abort delivered nothing: roll the buffered rows back so
    // the next dispatch rebuilds the same frame for the same page.
    if (!shotStreamSendCleanAbort) return false;
    session.statsSent = sentBase;
    return true;
  }
  if (more) {
    // Continue the page on the next dispatch; urgent bypasses the idle
    // cadence so a healthy client still streams without gaps.
    shotStreamUrgent_.store(true, std::memory_order_release);
    return true;
  }
  session.statsPaging = false;
  session.statsFetch = false;
  if (!fetch) {
    session.statsEpoch = epoch;
    session.statsFingerprint = fingerprint;
  } else if (session.statsRedeliverStanding) {
    // The preempted standing page re-arms now, with no page in flight.
    session.statsRedeliverStanding = false;
    session.statsResync = true;
  }
  return true;
}
