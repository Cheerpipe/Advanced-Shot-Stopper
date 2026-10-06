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
  delta.field("cupPresence.state", "\"%s\"", cupPresenceStateName(control.cupPresenceState));
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
  if (!owner || httpd_ws_send_frame_async(server_, session.fd, &frame) != ESP_OK) return false;
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
  delta.field("cupPresence.state", "\"%s\"", cupPresenceStateName(control.cupPresenceState));
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
    delta.field("lineaMicra.sampleAgeMs", "%lu",
                static_cast<unsigned long>(micra.sampleAtMs == 0 ? 0 : static_cast<uint32_t>(millis() - micra.sampleAtMs)));
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
  if (!owner || httpd_ws_send_frame_async(server_, session.fd, &frame) != ESP_OK) return false;
  session.diagBoot = control.bootId;
  session.diagResync = false;
  return true;
}
