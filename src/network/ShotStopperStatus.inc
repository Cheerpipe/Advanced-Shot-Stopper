const char *ShotStopperNetwork::wifiScanStateName(WifiScanState state) {
  switch (state) {
    case WifiScanState::IDLE: return "IDLE";
    case WifiScanState::QUEUED: return "QUEUED";
    case WifiScanState::RUNNING: return "RUNNING";
    case WifiScanState::READY: return "READY";
    case WifiScanState::FAILED: return "FAILED";
    case WifiScanState::CANCELED: return "CANCELED";
  }
  return "UNKNOWN";
}

esp_err_t ShotStopperNetwork::statusHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  // Settings stays claim-gated only. Admin mutations and the full
  // privileged status body require a temporary device-password unlock;
  // Diagnostic is intentionally public: /diagnostic stays reachable and the
  // visibility toggle only hides the menu entry.
  self.requestPendingNetworkConfirm();

  const StatusPage page = parseStatusPage(request->uri);
  if (page == StatusPage::Unknown) {
    return sendError(request, "404 Not Found", "STATUS_PAGE_UNKNOWN",
                     "Unknown status page; use /api/v1/status/{settings|admin|diagnostic}.");
  }

  // JSON / snapshot work lives in one PSRAM-only blob.
  if (!self.lockWorkBufForStatus()) {
    return self.workBufBusy(request);
  }

  ControlStatusSnapshot &control = self.workBuf_->control;
  self.loadControlStatus(control);
  const bool configMutable = controlAllowsConfiguration(control);
  const bool webUiOverrideActive = self.webUiOverrideAllowed(request);
  const uint32_t webUiOverrideRemainingMs =
      webUiOverrideActive ? self.webUiOverrideRemainingMs(request) : 0;
  bool adminUnlocked = false;
  if (page == StatusPage::Admin || page == StatusPage::Diagnostic) {
    adminUnlocked = self.adminUnlockAllowed(request);
    if (adminUnlocked &&
        (page == StatusPage::Admin || page == StatusPage::Diagnostic)) {
      self.touchAdminUnlock();
    }
  }
  const NetworkStatusSnapshot network = self.snapshot();
  const TimeStatusSnapshot timeStatus = g_wallClock.snapshot(millis());
  int16_t appliedTimezoneOffset = 0;
  const bool timezoneOffsetKnown = timeStatus.utcSec != 0 &&
      timeZoneOffsetAtOrUtc(control.config.timezoneId, timeStatus.utcSec,
                              appliedTimezoneOffset);
  int16_t lastSyncTimezoneOffset = 0;
  const bool lastSyncOffsetKnown = timeStatus.lastSyncUtcSec != 0 &&
      timeZoneOffsetAtOrUtc(control.config.timezoneId, timeStatus.lastSyncUtcSec,
                              lastSyncTimezoneOffset);
#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD
  LineaMicraStatus micraStatus = {};
  LineaMicraDiscoverySnapshot micraDiscovery = {};
  char safeMicraUsername[LINEA_MICRA_USERNAME_CAPACITY] = {};
  char micraSerial[LINEA_MICRA_SERIAL_CAPACITY] = {};
  char safeMicraSerial[LINEA_MICRA_SERIAL_CAPACITY] = {};
  char micraName[LINEA_MICRA_NAME_CAPACITY] = {};
  char safeMicraName[LINEA_MICRA_NAME_CAPACITY] = {};
  uint8_t micraOptions = 0;
  uint8_t micraScaleOptions = 0;
  uint8_t micraConnectionType = 0;
  bool micraAccountConfigured = false;
  self.dataMux_.lock();
  micraAccountConfigured = self.settings_.lineaMicra.accountConfigured;
  micraOptions = self.settings_.lineaMicra.options;
  micraScaleOptions = self.settings_.lineaMicra.scaleOptions;
  micraConnectionType = self.settings_.lineaMicra.connectionType;
  memcpy(safeMicraUsername, self.settings_.lineaMicra.username,
         sizeof(safeMicraUsername));
  memcpy(micraSerial, self.settings_.lineaMicra.selectedSerial,
         sizeof(micraSerial));
  memcpy(micraName, self.settings_.lineaMicra.selectedName,
         sizeof(micraName));
  self.dataMux_.unlock();
  micraStatus = machineIntegrationStatus();
  micraDiscovery = machineIntegrationDiscovery();
  sanitizeJsonEmbed(safeMicraUsername, safeMicraUsername,
                    sizeof(safeMicraUsername));
  sanitizeJsonEmbed(micraSerial, safeMicraSerial, sizeof(safeMicraSerial));
  sanitizeJsonEmbed(micraName, safeMicraName, sizeof(safeMicraName));
#endif

  char cupWeight[32] = "null";
  const auto &tare = control.cupTare;
  const uint8_t idleCode = idleTarePresentationCode(tare, control.cupPresent);
  const char *idleTare = idleCode == IDLE_WAITING_FOR_SETTLE ? "empty" : idleCode == IDLE_READY_FOR_CUP ? "ready"
      : idleTarePresentationName(idleCode);
  const bool cupWeightValid = control.cupPresent && control.cupTare.weightValid &&
                             std::isfinite(control.cupTare.weightG);
  if (cupWeightValid)
    snprintf(cupWeight, sizeof(cupWeight), "%.1f",
             static_cast<double>(control.cupTare.weightG));
  char staRssiJson[16] = "null";
  char staSignalQualityJson[16] = "null";
  char staChannelJson[8] = "null";
  char scaleRssiJson[16] = "null";
  char scaleWeightUpdateIntervalJson[16] = "null";
  char scaleTimerJson[16] = "null";
  char scaleObservedWeightJson[16] = "null";
  if (network.staLinkMetricsValid) {
    snprintf(staRssiJson, sizeof(staRssiJson), "%d",
             static_cast<int>(network.staRssi));
    snprintf(staSignalQualityJson, sizeof(staSignalQualityJson), "%u",
             static_cast<unsigned>(network.staSignalQualityPct));
  }
  if (network.staState == StaState::CONNECTED && network.channel != 0) {
    snprintf(staChannelJson, sizeof(staChannelJson), "%u",
             static_cast<unsigned>(network.channel));
  }
  if (control.scaleRssiValid) {
    snprintf(scaleRssiJson, sizeof(scaleRssiJson), "%d",
             static_cast<int>(control.scaleRssi));
  }
  if (page == StatusPage::Diagnostic &&
      control.weightStreamState == WeightStreamState::FRESH &&
      control.scaleWeightUpdateIntervalMs != 0) {
    snprintf(scaleWeightUpdateIntervalJson,
             sizeof(scaleWeightUpdateIntervalJson), "%lu",
             static_cast<unsigned long>(control.scaleWeightUpdateIntervalMs));
  }
  if (page == StatusPage::Diagnostic && control.scaleAvailable &&
      control.currentTimerValid) {
    snprintf(scaleTimerJson, sizeof(scaleTimerJson), "%lu",
             static_cast<unsigned long>(control.currentTimerMs / 100 * 100));
  }
  if (page == StatusPage::Diagnostic && control.scaleAvailable) {
    if (control.observedWeightValid && std::isfinite(control.observedWeightG))
      snprintf(scaleObservedWeightJson, sizeof(scaleObservedWeightJson),
               "%.1f", static_cast<double>(control.observedWeightG));
    else if (control.currentWeightValid && std::isfinite(control.currentWeightG))
      snprintf(scaleObservedWeightJson, sizeof(scaleObservedWeightJson),
               "%.1f", static_cast<double>(control.currentWeightG));
  }

  char safeNtpCustom[NTP_SERVER_HOST_CAPACITY] = {};
  char safeActiveServer[NTP_SERVER_HOST_CAPACITY] = {};
  char safePreferredScaleMac[PREFERRED_SCALE_MAC_CAPACITY * 2] = {};
  char safePreferredScaleName[PREFERRED_SCALE_NAME_CAPACITY * 2] = {};
  char safeFirmwareVersion[32] = {};
  char safeStaSsid[WIFI_SSID_CAPACITY] = {};
  char safeApSsid[WIFI_SSID_CAPACITY] = {};
  char safeDeviceName[DEVICE_NAME_CAPACITY] = {};
  char safeMdnsHost[DEVICE_NAME_CAPACITY] = {};
  char safeWebhookUrl[WEBHOOK_URL_CAPACITY * 2] = {};
  sanitizeJsonEmbed(control.config.ntpServerCustom, safeNtpCustom,
                    sizeof(safeNtpCustom));
  sanitizeJsonEmbed(timeStatus.activeServer, safeActiveServer,
                    sizeof(safeActiveServer));
  sanitizeJsonEmbed(control.preferredScaleMac, safePreferredScaleMac,
                    sizeof(safePreferredScaleMac));
  sanitizeJsonEmbed(control.preferredScaleName, safePreferredScaleName,
                    sizeof(safePreferredScaleName));
  sanitizeJsonEmbed(FW_VERSION, safeFirmwareVersion,
                    sizeof(safeFirmwareVersion));
  sanitizeJsonEmbed(network.staSsid, safeStaSsid, sizeof(safeStaSsid));
  sanitizeJsonEmbed(network.apSsid, safeApSsid, sizeof(safeApSsid));
  sanitizeJsonEmbed(network.deviceName, safeDeviceName, sizeof(safeDeviceName));
  sanitizeJsonEmbed(network.mdnsHost, safeMdnsHost, sizeof(safeMdnsHost));
  const WebhookConfig webhookConfig = self.webhooks_.config();
  const WebhookStatus webhookStatus = self.webhooks_.status();
  sanitizeJsonEmbed(webhookConfig.url, safeWebhookUrl,
                    sizeof(safeWebhookUrl));
  if (page == StatusPage::Settings &&
      self.callbacks_.copyBullseyeConfig != nullptr) {
    self.callbacks_.copyBullseyeConfig(&g_work->bullseyeMelody);
    sanitizeJsonEmbed(g_work->bullseyeMelody.rtttl,
                      g_work->safeBullseyeRtttl,
                      sizeof(g_work->safeBullseyeRtttl));
  }

  const bool needPresets = page == StatusPage::Settings;
  const bool needHistory = page == StatusPage::Settings;
  // Every header names the connected scale; the history array stays Settings-only.
  if (needPresets) {
    if (self.callbacks_.copyPresetBank != nullptr) {
      self.callbacks_.copyPresetBank(&g_work->presetBank);
    } else {
      memset(&g_work->presetBank, 0, sizeof(g_work->presetBank));
    }
    buildSlimPresetsJson(g_work->presetBank);
  } else {
    g_work->presetsJson[0] = '{';
    g_work->presetsJson[1] = '}';
    g_work->presetsJson[2] = 0;
  }
  if (self.callbacks_.copyScaleHistory != nullptr) {
    self.callbacks_.copyScaleHistory(g_work->scaleHistory);
  } else {
    memset(&g_work->scaleHistory, 0, sizeof(g_work->scaleHistory));
  }
  if (needHistory) {
    buildScaleHistoryJson(g_work->scaleHistory);
  } else {
    g_work->historyJson[0] = '[';
    g_work->historyJson[1] = ']';
    g_work->historyJson[2] = 0;
  }
  char preferredScaleFriendly[PREFERRED_SCALE_NAME_CAPACITY] = {};
  findScaleHistoryFriendlyName(g_work->scaleHistory, control.preferredScaleMac,
                               preferredScaleFriendly,
                               sizeof(preferredScaleFriendly));
  char safePreferredScaleFriendly[PREFERRED_SCALE_NAME_CAPACITY * 2] = {};
  sanitizeJsonEmbed(preferredScaleFriendly, safePreferredScaleFriendly,
                    sizeof(safePreferredScaleFriendly));
  char connectedScaleFriendly[PREFERRED_SCALE_NAME_CAPACITY] = {};
  findScaleHistoryFriendlyName(g_work->scaleHistory, control.connectedScaleMac,
                               connectedScaleFriendly,
                               sizeof(connectedScaleFriendly));
  char safeConnectedScaleMac[PREFERRED_SCALE_MAC_CAPACITY * 2] = {};
  char safeConnectedScaleFriendly[PREFERRED_SCALE_NAME_CAPACITY * 2] = {};
  sanitizeJsonEmbed(control.connectedScaleMac, safeConnectedScaleMac,
                    sizeof(safeConnectedScaleMac));
  sanitizeJsonEmbed(connectedScaleFriendly, safeConnectedScaleFriendly,
                    sizeof(safeConnectedScaleFriendly));
  char wifiName[WIFI_SSID_CAPACITY * 6] = {};
  char bluetoothName[PREFERRED_SCALE_NAME_CAPACITY * 6] = {};
  escapeJsonString(network.staState == StaState::CONNECTED ? network.staSsid : "",
                   wifiName, sizeof(wifiName));
  escapeConnectedScaleName(control, g_work->scaleHistory,
                           bluetoothName, sizeof(bluetoothName));
  size_t used = 0;
  const bool liveShot =
      control.activeCycle || control.relayClosed;
  // Shared envelope only: page-specific config (NTP, serial debug, buzzer)
  // is appended below so home/settings polls stay lean.
  bool ok = statusJsonAppend(
      &used,
      "{\"firmwareVersion\":\"%s\",\"bootId\":%lu,\"configMutable\":%s,"
      "\"snapshotStale\":%s,"
      "\"webUiOverrideActive\":%s,\"webUiOverrideRemainingMs\":%lu,"
      "\"configLockReason\":\"%s\",\"liveShot\":%s",
      safeFirmwareVersion,
      static_cast<unsigned long>(control.bootId),
      configMutable ? "true" : "false",
      control.snapshotStale ? "true" : "false",
      webUiOverrideActive ? "true" : "false",
      static_cast<unsigned long>(webUiOverrideRemainingMs),
      configLockReason(control), liveShot ? "true" : "false");
  if (ok) {
    ok = statusJsonAppend(
        &used, ",\"machineType\":\"%s\",\"machineIntegration\":\"%s\","
        "\"connections\":{\"wifiConnected\":%s,\"wifiLevel\":%s,"
        "\"bluetoothConnected\":%s,\"bluetoothLevel\":%s,"
        "\"wifiName\":\"%s\",\"bluetoothName\":\"%s\"}",
        compiledMachineTypeId(),
#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD
        "linea_micra_cloud",
#else
        "none",
#endif
        network.staState == StaState::CONNECTED ? "true" : "false",
        connectionSignalLevelJson(network.staLinkMetricsValid, network.staRssi),
        control.scaleAvailable ? "true" : "false",
        connectionSignalLevelJson(control.scaleRssiValid, control.scaleRssi),
        wifiName, bluetoothName);
  }
  if (ok && page == StatusPage::Diagnostic) {
    ok = statusJsonAppend(&used,
        ",\"backflush\":{\"waiting\":%s,\"ready\":%s,\"unresolved\":%s,"
        "\"stopReason\":\"%s\",\"hardLimitMs\":%lu,\"remainingMs\":%lu}",
        control.backflushWaiting ? "true" : "false", control.backflushReady ? "true" : "false",
        control.backflushUnresolved ? "true" : "false", control.backflushStopReason,
        static_cast<unsigned long>(control.safetyHardLimitMs),
        static_cast<unsigned long>(control.safetyRemainingMs));
  }
  if (ok && (page == StatusPage::Admin || page == StatusPage::Diagnostic)) {
    const PowerProfile applied = powerAppliedProfile.load(std::memory_order_acquire);
    ok = statusJsonAppend(&used,
        ",\"power\":{\"enabled\":%s,\"requestedProfile\":\"%s\",\"profile\":\"%s\",\"cpuMhz\":%u,"
        "\"minMhz\":%d,\"maxMhz\":%d,\"clockError\":%d,\"bleError\":%d,"
        "\"bleSleep\":%s,\"webActive\":%s,\"cooldownMs\":%lu}",
        control.config.powerManagementEnabled ? "true" : "false",
        powerProfileName(powerRequestedProfile.load(std::memory_order_acquire)),
        powerProfileName(applied), static_cast<unsigned>(getCpuFrequencyMhz()),
        powerMinMhz(applied), powerMaxMhz(applied), powerClockError.load(),
        powerBleError.load(), powerBleSleeping.load() ? "true" : "false",
        powerWebActive(millis()) ? "true" : "false",
        static_cast<unsigned long>(powerCooldownRemainingMs.load()));
  }
  if (ok) {
    ok = statusJsonAppend(&used, ",\"diagnosticPageVisible\":%s",
                          self.diagnosticPageEnabled() ? "true" : "false");
  }
  if (ok) {
    ok = statusJsonAppend(&used, ",\"compatibilityMode\":%s",
                          firmwareCompatibilityMode() ? "true" : "false");
  }
  if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(&used, ",\"buzzerSupported\":%s",
                          BUZZER_SUPPORT_ENABLED ? "true" : "false");
  }
  if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(
        &used, ",\"lastCommand\":{\"requestId\":%lu,\"state\":\"%s\"}",
        static_cast<unsigned long>(network.lastCommandRequestId),
        commandResultStateName(network.lastCommandState));
  }
  if (ok) {
    ok = statusJsonAppend(
        &used, ",\"timeUtcSec\":%lu,\"config\":{\"revision\":%lu,\"ringRetainLogLevel\":\"%s\",\"serialLogLevel\":\"%s\"",
        static_cast<unsigned long>(timeStatus.utcSec),
        static_cast<unsigned long>(control.config.revision),
        logLevelName(static_cast<LogLevel>(control.config.ringRetainLogLevel)),
        logLevelName(serialLogLevelFromRuntime(control.config)));
  }
  if (ok) {
    ok = statusJsonAppend(
        &used, ",\"timezoneId\":\"%s\",\"appliedTimezoneOffsetMinutes\":%d,"
        "\"timezoneOffsetKnown\":%s,\"lastSyncAppliedTimezoneOffsetMinutes\":%d,"
        "\"lastSyncOffsetKnown\":%s,\"tzdbVersion\":\"%s\","
        "\"timezoneResolution\":\"%s\"",
        control.config.timezoneId, static_cast<int>(appliedTimezoneOffset),
        timezoneOffsetKnown ? "true" : "false",
        static_cast<int>(lastSyncTimezoneOffset),
        lastSyncOffsetKnown ? "true" : "false", tzdb::kVersion,
        timeZoneResolution(control.config.timezoneId, timeStatus.utcSec));
  }
  if (ok) {
    ok = statusJsonAppend(&used,
        ",\"timezoneAutomatic\":%s,\"timezoneInitialized\":%s",
        control.config.timezoneAutomatic ? "true" : "false",
        durableTimezoneSaved().load() ? "true" : "false");
  }
  if (ok && page == StatusPage::Admin && adminUnlocked) {
    ok = statusJsonAppend(
        &used,
        ",\"timezoneSource\":%u,"
        "\"firstTimezoneAutoUtcSec\":%lu,\"firstTimezoneAutoUptimeUs\":%llu,"
        "\"firstTimezoneAutoBootId\":%lu,\"firstTimezoneAutoClockQuality\":%u,"
        "\"ntpServerPreset\":\"%s\",\"ntpSyncEnabled\":%s,"
        "\"ntpServerCustom\":\"%s\",\"showDiagnosticPage\":%s,\"powerManagementEnabled\":%s,"
        "\"persistPending\":%s,\"persistFailed\":%s",
        static_cast<unsigned>(control.config.timezoneSource),
        static_cast<unsigned long>(control.config.firstTimezoneAutoUtcSec),
        static_cast<unsigned long long>(control.config.firstTimezoneAutoUptimeUs),
        static_cast<unsigned long>(control.config.firstTimezoneAutoBootId),
        static_cast<unsigned>(control.config.firstTimezoneAutoClockQuality),
        ntpPresetId(control.config.ntpServerPreset),
        control.config.ntpSyncEnabled ? "true" : "false", safeNtpCustom,
        control.config.showDiagnosticPage ? "true" : "false",
        control.config.powerManagementEnabled ? "true" : "false",
        control.configPersistPending ? "true" : "false",
        control.configPersistFailed ? "true" : "false");
  }
  if (ok && page == StatusPage::Diagnostic) {
    ok = statusJsonAppend(
        &used,
        ",\"serialDebugOutput\":%s",
        serialLogLevelFromRuntime(control.config) != LogLevel::NONE ? "true"
                                                                     : "false");
  }

  if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(
        &used,
        ",\"goalWeightG\":%u,\"weightOffsetG\":%.2f,"
        "\"weightOffsetBaselineG\":%.2f,\"autoTare\":%s,"
        "\"autoTareOutsideBrew\":%s,\"retareAccessoryOutsideBrew\":%s,"
        "\"postTareBaselineGraceMs\":%lu,\"brewByWeight\":%s,"
        "\"canTareStartTimer\":%s,\"scaleTimerStopExtraDelayMs\":%lu,"
        "\"dripDelayMs\":%lu,"
        "\"soundAlertsEnabled\":%s,\"firstDropBeep\":%s,"
        "\"paddleReturnReminderBeep\":%s,"
        "\"paddleReturnReminderIntervalMs\":%lu,"
        "\"paddleReturnReminderMaxDurationMs\":%lu,\"paddleMode\":\"%s\","
        "\"stopPulseMs\":%lu,\"maxSinglePressMs\":%lu,"
        "\"momentaryStartEdge\":\"%s\",\"reedConfirmTimeoutMs\":%lu,"
        "\"assumeIdleWhenScaleConnects\":%s,\"shotReactTimeoutS\":%u,"
        "\"buzzerScaleLostBeep\":%s,\"buzzerAutoToManualGuardEndBeep\":%s,"
        "\"buzzerManualNoScaleBeep\":%s,\"buzzerScaleConnectedBeep\":%s,"
        "\"scaleConnectedLed\":%s,"
        "\"buzzerExtendedPulseRate\":\"%s\","
        "\"buzzerSlowExtendedPulseRate\":\"%s\","
        "\"alertOutputChannel\":\"%s\",\"rinseEnabled\":%s,\"rinseGestureMs\":%lu,"
        "\"rinseDurationMs\":%lu,\"autoRetare\":%s,\"retareWindowMs\":%lu,"
        "\"minimumCupWeightG\":%.1f,\"retareStabilitySamples\":%u,"
        "\"retareStabilityToleranceG\":%.1f,\"retareStabilityMaxGapMs\":%lu,"
        "\"retareStabilityMinDurationMs\":%lu,\"bbwProtectionMs\":%lu,"
        "\"operationalWallMs\":%lu,\"fastExtractionGuardEnabled\":%s,"
        "\"maxRecoveryWeightG\":%.1f,\"minBbwBrewTimeMs\":%lu,"
        "\"slowExtractionGuardEnabled\":%s,\"minRecoveryWeightG\":%.1f,"
        "\"maxBbwBrewTimeMs\":%lu,\"autoToManualGuardEnabled\":%s,"
        "\"autoToManualGuardLimitMode\":\"%s\","
        "\"autoToManualGuardManualLimitMs\":%lu,"
        "\"autoToManualGuardBaselineMs\":%lu,"
        "\"autoToManualGuardTrendMs\":%lu,\"cupProtectionEnabled\":%s,"
        "\"stopIfCupRemoved\":%s,\"requireCupToStart\":%s,"
        "\"avoidAccidentalTouchEnabled\":%s,"
        "\"touchStopFallbackEnabled\":%s,"
        "\"cupPresentWeightG\":%.1f,\"cupRemovedWeightG\":%.1f,"
        "\"scaleMacCacheMode\":\"%s\","
        "\"bookooMuteOnBuzzerOnly\":%s,\"bookooConnectBeepLevel\":%u,"
        "\"avoidBbwShotWithoutScale\":%s,\"noScaleBbwMode\":\"%s\","
        "\"noScaleAllowRinseWhileArmed\":%s,\"lastShotCooldownMs\":%lu",
        static_cast<unsigned>(control.config.goalWeightG),
        static_cast<double>(control.config.weightOffsetG),
        static_cast<double>(control.config.weightOffsetBaselineG),
        control.config.autoTare ? "true" : "false",
        control.config.autoTareOutsideBrew ? "true" : "false",
        idleAccessoryRetareEnabled(control.config.noScaleBbwMode) ? "true" : "false",
        static_cast<unsigned long>(control.config.postTareBaselineGraceMs),
        control.config.timerOnly ? "false" : "true",
        control.config.canTareStartTimer ? "true" : "false",
        static_cast<unsigned long>(control.config.scaleTimerStopExtraDelayMs),
        static_cast<unsigned long>(control.config.dripDelayMs),
        control.config.soundAlertsMuted ? "false" : "true",
        control.config.firstDropBeep ? "true" : "false",
        control.config.paddleReturnReminderBeep ? "true" : "false",
        static_cast<unsigned long>(
            control.config.paddleReturnReminderIntervalMs),
        static_cast<unsigned long>(
            control.config.paddleReturnReminderMaxDurationMs),
        paddleModeId(control.config.paddleMode),
        static_cast<unsigned long>(runtimeStopPulseMs(control.config)),
        static_cast<unsigned long>(runtimeMaxSinglePressMs(control.config)),
        momentaryStartEdgeId(control.config.momentaryStartOnPress),
        static_cast<unsigned long>(
            runtimeReedConfirmTimeoutMs(control.config)),
        control.config.assumeIdleWhenScaleConnects ? "true" : "false",
        static_cast<unsigned>(runtimeShotReactTimeoutS(control.config)),
        control.config.buzzerScaleLostBeep ? "true" : "false",
        control.config.buzzerAutoToManualGuardEndBeep ? "true" : "false",
        control.config.buzzerManualNoScaleBeep ? "true" : "false",
        control.config.buzzerScaleConnectedBeep ? "true" : "false",
        control.config.scaleConnectedLed ? "true" : "false",
        extendedPulseRateId(control.config.buzzerExtendedPulseRate),
        extendedPulseRateId(control.config.buzzerSlowExtendedPulseRate),
        alertOutputChannelId(control.config.alertOutputChannel),
        control.config.rinseEnabled ? "true" : "false",
        static_cast<unsigned long>(control.config.rinseGestureMs),
        static_cast<unsigned long>(control.config.rinseDurationMs),
        control.config.autoRetare ? "true" : "false",
        static_cast<unsigned long>(control.config.retareWindowMs),
        static_cast<double>(control.config.minimumCupWeightG),
        static_cast<unsigned>(control.config.retareStabilitySamples),
        static_cast<double>(control.config.retareStabilityToleranceG),
        static_cast<unsigned long>(control.config.retareStabilityMaxGapMs),
        static_cast<unsigned long>(control.config.retareStabilityMinDurationMs),
        static_cast<unsigned long>(control.config.bbwProtectionMs),
        static_cast<unsigned long>(control.config.operationalWallMs),
        control.config.fastExtractionGuardEnabled ? "true" : "false",
        static_cast<double>(control.config.maxRecoveryWeightG),
        static_cast<unsigned long>(control.config.minBbwBrewTimeMs),
        control.config.slowExtractionGuardEnabled ? "true" : "false",
        static_cast<double>(control.config.minRecoveryWeightG),
        static_cast<unsigned long>(control.config.maxBbwBrewTimeMs),
        control.config.autoToManualGuardEnabled ? "true" : "false",
        autoToManualGuardLimitModeId(control.config.autoToManualGuardLimitMode),
        static_cast<unsigned long>(control.config.autoToManualGuardManualLimitMs),
        static_cast<unsigned long>(control.config.autoToManualGuardBaselineMs),
        static_cast<unsigned long>(control.autoToManualGuardTrendMs),
        control.config.cupProtectionEnabled ? "true" : "false",
        control.config.stopIfCupRemoved ? "true" : "false",
        control.config.requireCupToStart ? "true" : "false",
        control.config.avoidAccidentalTouchEnabled ? "true" : "false",
        control.config.touchStopFallbackEnabled ? "true" : "false",
        static_cast<double>(control.config.cupPresentWeightG),
        static_cast<double>(control.config.cupRemovedWeightG),
        scaleMacCacheModeId(control.config.scaleMacCacheMode),
        control.config.bookooMuteOnBuzzerOnly ? "true" : "false",
        static_cast<unsigned>(control.config.bookooConnectBeepLevel),
        noScaleBbwEnabled(control.config.noScaleBbwMode) ? "true" : "false",
        noScaleBbwModeId(control.config.noScaleBbwMode),
        noScaleAllowRinseWhileArmed(control.config.noScaleBbwMode) ? "true"
                                                                 : "false",
        static_cast<unsigned long>(control.config.lastShotCooldownMs));
  }

  if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(
        &used, ",\"bbwAlgorithm\":\"%s\",\"bbwPresetId\":%u,"
        "\"bbwLegacyOffsetG\":%.2f,\"bbwEwmaOffsetG\":%.2f,"
        "\"bbwAlpha\":%.2f,\"bbwAlphaBaseline\":%.2f,\"bbwAlphaSource\":\"%s\",\"bbwEvidenceCount\":%u",
        bbwAlgorithmName(control.config.bbwAlgorithm), control.bbwPresetId,
        static_cast<double>(control.bbwLegacyOffsetG),
        static_cast<double>(control.bbwEwmaOffsetG), control.bbwAlpha / 100.0,
        control.bbwAlphaBaseline / 100.0,
        control.bbwAlphaLearned ? "learned" : "initial", control.bbwEvidenceCount);
  }
  if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(
        &used, ",\"bullseyeMelodyEnabled\":%s,\"bullseyeRtttl\":\"%s\"",
        g_work->bullseyeMelody.enabled ? "true" : "false",
        g_work->safeBullseyeRtttl);
  }

  if (ok) {
    ok = statusJsonAppend(&used, "}");
  }

#include "ShotStopperLineaMicraStatus.inc"

  if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(
        &used,
        ",\"scale\":{\"preferredMac\":\"%s\",\"preferredName\":\"%s\","
        "\"preferredFriendlyName\":\"%s\","
        "\"macCachePauseRemainingMs\":%lu,\"history\":%s},"
        "\"presets\":%s,\"stopPulseMs\":%lu,\"maxSinglePressMs\":%lu,"
        "\"momentaryStartEdge\":\"%s\",\"reedConfirmTimeoutMs\":%lu,"
        "\"assumeIdleWhenScaleConnects\":%s,\"shotReactTimeoutS\":%u",
        safePreferredScaleMac, safePreferredScaleName,
        safePreferredScaleFriendly,
        static_cast<unsigned long>(control.scaleMacCachePauseRemainingMs),
        g_work->historyJson, g_work->presetsJson,
        static_cast<unsigned long>(runtimeStopPulseMs(control.config)),
        static_cast<unsigned long>(runtimeMaxSinglePressMs(control.config)),
        momentaryStartEdgeId(control.config.momentaryStartOnPress),
        static_cast<unsigned long>(
            runtimeReedConfirmTimeoutMs(control.config)),
        control.config.assumeIdleWhenScaleConnects ? "true" : "false",
        static_cast<unsigned>(runtimeShotReactTimeoutS(control.config)));
  } else if (ok && page == StatusPage::Admin) {
    // Locked admin: confirm-window hint only. Unlocked: Wi-Fi/AP + BLE + OTA.
    ok = statusJsonAppend(&used, ",\"adminUnlocked\":%s,\"development\":%s",
                          adminUnlocked ? "true" : "false",
                          DEVELOPMENT_BUILD ? "true" : "false");
    if (ok && !adminUnlocked) {
      ok = statusJsonAppend(
          &used,
          ",\"network\":{\"configState\":\"%s\",\"confirmRemainingMs\":%lu}",
          staConfigStateName(network.staConfigState),
          static_cast<unsigned long>(network.confirmRemainingMs));
    } else if (ok) {
      // Admin page: Wi-Fi/AP status + STA address form hydration only.
      // Diagnostics metrics live on status/diagnostic.
      ok = statusJsonAppend(
          &used,
          ",\"network\":{\"apActive\":%s,\"apSsid\":\"%s\",\"apIp\":\"%s\",\"apClients\":%u,"
          "\"wifiConfigured\":%s,\"ssid\":\"%s\",\"open\":%s,\"wifiSleep\":%s,"
          "\"staState\":\"%s\",\"channel\":%s,\"staIp\":\"%s\",\"ipMode\":\"%s\","
          "\"configState\":\"%s\",\"confirmRemainingMs\":%lu,"
          "\"rssi\":%s,\"signalQualityPct\":%s,"
          "\"configuredIp\":\"%s\",\"configuredNetmask\":\"%s\","
          "\"configuredGateway\":\"%s\",\"configuredDns1\":\"%s\","
          "\"configuredDns2\":\"%s\","
          "\"deviceName\":\"%s\",\"mdnsHost\":\"%s\"},"
          "\"bleScan\":{\"scanIntensity\":\"%s\",\"enabled\":%s}",
          network.apActive ? "true" : "false", safeApSsid, network.apIp,
          static_cast<unsigned>(network.apClients),
          network.wifiConfigured ? "true" : "false", safeStaSsid,
          network.staOpen ? "true" : "false",
          network.staWifiSleep ? "true" : "false", staStateName(network.staState),
          staChannelJson, network.staIp, staIpModeName(network.staIpMode),
          staConfigStateName(network.staConfigState),
          static_cast<unsigned long>(network.confirmRemainingMs), staRssiJson,
          staSignalQualityJson, network.configuredIp, network.configuredNetmask,
          network.configuredGateway, network.configuredDns1,
          network.configuredDns2, safeDeviceName, safeMdnsHost,
          bleScanIntensityName(clampBleScanIntensity(
              control.bleScanIntensity)),
          control.bleScanEnabled ? "true" : "false");
      if (ok) {
        ok = statusJsonAppend(
            &used,
            ",\"webhooks\":{\"enabled\":%s,\"url\":\"%s\","
            "\"brewState\":%s,\"firstDrop\":%s,\"end\":%s,"
            "\"deferDuringShot\":%s,"
            "\"workerReady\":%s,\"sending\":%s,\"lastSuccess\":%s,"
            "\"lastHttpStatus\":%u,\"lastError\":%ld,"
            "\"lastEvent\":\"%s\",\"lastEndpoint\":\"%s\","
            "\"lastPhase\":\"%s\",\"lastCancellation\":\"%s\","
            "\"lastDispatched\":%s,\"lastDeliveryUnknown\":%s,"
            "\"lastAttemptAtMs\":%lu,\"sent\":%lu,\"dropped\":%lu,"
            "\"staleConfigDropped\":%lu,\"workerStarts\":%lu,"
            "\"workerStops\":%lu,\"workerStartFailures\":%lu,"
            "\"clientCreates\":%lu,\"clientReuses\":%lu,"
            "\"transportResets\":%lu,\"clientCleanups\":%lu,"
            "\"heapSamples\":%lu,\"internalFreeBefore\":%lu,"
            "\"internalFreeAfter\":%lu,\"internalLargestBefore\":%lu,"
            "\"internalLargestAfter\":%lu,\"internalLargestMinimum\":%lu,"
            "\"psramLargestBefore\":%lu,\"psramLargestAfter\":%lu},"
            "\"lastCommand\":{\"requestId\":%lu,\"state\":\"%s\"}",
            webhookConfig.enabled ? "true" : "false", safeWebhookUrl,
            webhookConfig.brewState ? "true" : "false",
            webhookConfig.firstDrop ? "true" : "false",
            webhookConfig.end ? "true" : "false",
            webhookConfig.deferDuringShot ? "true" : "false",
            webhookStatus.workerReady ? "true" : "false",
            webhookStatus.sending ? "true" : "false",
            webhookStatus.lastSuccess ? "true" : "false",
            static_cast<unsigned>(webhookStatus.lastHttpStatus),
            static_cast<long>(webhookStatus.lastError),
            webhookStatus.lastEvent, webhookStatus.lastEndpoint,
            webhookRequestPhaseName(webhookStatus.lastPhase),
            webhookCancellationReasonName(webhookStatus.lastCancellation),
            webhookStatus.lastDispatched ? "true" : "false",
            webhookStatus.lastDeliveryUnknown ? "true" : "false",
            static_cast<unsigned long>(webhookStatus.lastAttemptAtMs),
            static_cast<unsigned long>(webhookStatus.sent),
            static_cast<unsigned long>(webhookStatus.dropped),
            static_cast<unsigned long>(webhookStatus.staleConfigDropped),
            static_cast<unsigned long>(webhookStatus.workerStarts),
            static_cast<unsigned long>(webhookStatus.workerStops),
            static_cast<unsigned long>(webhookStatus.workerStartFailures),
            static_cast<unsigned long>(webhookStatus.clientCreates),
            static_cast<unsigned long>(webhookStatus.clientReuses),
            static_cast<unsigned long>(webhookStatus.transportResets),
            static_cast<unsigned long>(webhookStatus.clientCleanups),
            static_cast<unsigned long>(webhookStatus.heapSamples),
            static_cast<unsigned long>(webhookStatus.internalFreeBefore),
            static_cast<unsigned long>(webhookStatus.internalFreeAfter),
            static_cast<unsigned long>(webhookStatus.internalLargestBefore),
            static_cast<unsigned long>(webhookStatus.internalLargestAfter),
            static_cast<unsigned long>(webhookStatus.internalLargestMinimum),
            static_cast<unsigned long>(webhookStatus.psramLargestBefore),
            static_cast<unsigned long>(webhookStatus.psramLargestAfter),
            static_cast<unsigned long>(network.lastCommandRequestId),
            commandResultStateName(network.lastCommandState));
      }
      if (ok) {
        self.buildOtaJson(g_work->otaJson, NetworkWorkBuf::kOtaJson,
                          controlGateOf(control), control.bootId);
        ok = statusJsonAppend(&used, ",\"boardArch\":\"%s\",\"ota\":%s",
                              FW_BOARD_ARCH_STRING, g_work->otaJson);
      }
    }
  } else if (ok && page == StatusPage::Diagnostic) {
    const uint32_t acquisitionGeneration = outboundAcquisitionGeneration.load(std::memory_order_acquire);
    const bool acquisitionHeld = outboundAcquisitionHeld.load(std::memory_order_acquire);
    ok = statusJsonAppend(&used, ",\"outboundPause\":{\"generation\":%u,\"requested\":%s,\"atMs\":%u,\"scaleSetup\":%s,\"bleQuiet\":%s,\"shot\":%s,\"maintenance\":%s,\"reason\":\"%s\",\"clients\":[",
        unsigned(acquisitionGeneration), acquisitionHeld ? "true" : "false", unsigned(outboundAcquisitionAtMs.load()),
        outboundScaleSetup.load() ? "true" : "false", outboundBleQuiet.load() ? "true" : "false",
        outboundShotActive.load() ? "true" : "false", outboundMaintenance.load() || outboundOtaBusy.load() ? "true" : "false",
        acquisitionHeld ? "scale_acquisition" : outboundBleQuiet.load() ? "ble_quiet" : outboundScaleSetup.load() ? "scale_setup" : outboundShotActive.load() ? "shot" : outboundMaintenance.load() || outboundOtaBusy.load() ? "maintenance" : "none");
    static constexpr const char *clients[] = {"micra_http", "micra_ws", "webhook", "ntp"};
    for (unsigned i = 0; ok && i < static_cast<unsigned>(OutboundClient::COUNT); ++i) {
      const auto &completion = outboundPauseCompletions[i];
      constexpr bool micraClients = SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD;
      ok = statusJsonAppend(&used, "%s{\"client\":\"%s\",\"applicable\":%s,\"completed\":%s,\"latencyMs\":%u}", i ? "," : "", clients[i],
          i >= 2 || (micraClients && i == 0) ? "true" : "false",
          acquisitionHeld && acquisitionGeneration == completion.generation.load(std::memory_order_acquire) ? "true" : "false",
          unsigned(completion.latencyMs.load()));
    }
    ok = ok && statusJsonAppend(&used, "]}");
    const NetworkHeapTelemetrySnapshot networkHeap =
        self.heapTelemetrySnapshot();
    const HeapLifecycleAggregate webhookHeap =
        self.webhooks_.heapTelemetry();
    HeapLifecycleAggregate micraHeap = {};
    HeapLifecycleAggregate micraWsHeap = {};
#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD
    micraHeap = machineIntegrationHeapTelemetry();
    micraWsHeap = machineIntegrationWebSocketStatus().heap;
#endif
    // Reading the diagnostic page is public while it is enabled; adminUnlocked
    // reflects the caller's real Admin session (controls chrome on the UI).
    ok = statusJsonAppend(&used, ",\"adminUnlocked\":%s,\"diagnosticPublic\":true,\"development\":%s",
                          adminUnlocked ? "true" : "false",
                          DEVELOPMENT_BUILD ? "true" : "false");
    if (ok) {
      const CrashArchiveStatus crash = crashArchiveStatus();
      ok = statusJsonAppend(&used, ",\"crashCount\":%u,\"crashState\":%u",
                            crash.count, static_cast<unsigned>(crash.state));
    }
    if (ok) {
    // Lean diagnostic snapshot: metrics + log controls only (no STA address
    // form fields; those stay on status/admin).
    const ScaleBleDiagnostics &ble = control.scaleBleDiagnostics;
    char disconnectSummary[144] = {};
    char commandSummary[64] = {};
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
    char supportedCommandsJson[320] = "[";
    size_t commandBytes = 1;
    ScaleCommandInfo commandInfo;
    for (size_t i = 0;
         scaleBookooCommandAt(control.scaleModel, i, &commandInfo); ++i) {
      const int n = snprintf(supportedCommandsJson + commandBytes,
                             sizeof(supportedCommandsJson) - commandBytes,
                             "%s{\"name\":\"%s\",\"code\":\"0x%02X\"}",
                             i == 0 ? "" : ",", commandInfo.name,
                             static_cast<unsigned>(commandInfo.code));
      if (n < 0 || static_cast<size_t>(n) >=
                       sizeof(supportedCommandsJson) - commandBytes) {
        commandBytes = 1;
        break;
      }
      commandBytes += static_cast<size_t>(n);
    }
    supportedCommandsJson[commandBytes++] = ']';
    supportedCommandsJson[commandBytes] = '\0';
    ok = statusJsonAppend(
        &used,
        ",\"state\":\"%s\",\"machineState\":\"%s\","
        "\"relayClosed\":%s,\"machineRunning\":%s,"
        "\"machineStartAck\":%s,\"machineStopAck\":%s,\"machineOrphan\":%s,"
        "\"reedOn\":%s,"
        "\"physicalActivatorOn\":%s,"
        "\"controlSource\":\"%s\","
        "\"cupPresence\":{\"state\":\"%s\",\"present\":%s,"
        "\"weightG\":%s,\"weightValid\":%s,\"placementId\":%lu,\"idleTare\":\"%s\"},"
        "\"network\":{\"apActive\":%s,\"apSsid\":\"%s\",\"apIp\":\"%s\",\"apClients\":%u,"
        "\"wifiConfigured\":%s,\"ssid\":\"%s\","
        "\"staState\":\"%s\",\"wifiPs\":\"%s\",\"wifiCoex\":\"%s\",\"channel\":%s,\"staIp\":\"%s\",\"ipMode\":\"%s\","
        "\"configState\":\"%s\",\"confirmRemainingMs\":%lu,"
        "\"rssi\":%s,\"signalQualityPct\":%s},"
        "\"time\":{\"state\":\"%s\",\"utcSec\":%lu,\"lastSyncAgeMs\":%lu,\"lastSyncUtcSec\":%lu,"
        "\"nextRetryInMs\":%lu,\"activeServer\":\"%s\"},"
        "\"maintenance\":{\"active\":%s,\"leaseId\":%lu,"
        "\"persistPending\":%s,\"persistFailed\":%s},"
        "\"health\":{\"stackUnit\":\"bytes\",\"stackUnavailable\":4294967295,"
        "\"loopStackMinBytes\":%lu,\"scaleStackMinBytes\":%lu,"
        "\"uptimeMs\":%lu,\"snapshotVersion\":%lu,"
        "\"snapshotStale\":%s,\"loopIntervalGapMs\":%lu,"
        "\"loopMaxGapMs\":%lu,\"loopDeadlineMisses\":%lu,"
        "\"loopMaxExecutionUs\":%lu,"
        "\"scaleWorkerMaxGapMs\":%lu,\"scaleWorkerDeadlineMisses\":%lu,"
        "\"scaleWorkerMaxExecutionUs\":%lu,"
        "\"healthSnapshotVersion\":%lu,\"healthSnapshotAgeMs\":%lu,"
        "\"healthSnapshotValid\":%s,"
        "\"freeHeapBytes\":%lu,\"minimumFreeHeapBytes\":%lu,"
        "\"largestFreeHeapBlockBytes\":%lu,"
        "\"internalHeapAllocatedBlocks\":%lu,"
        "\"internalHeapFreeBlocks\":%lu,\"internalHeapTotalBlocks\":%lu,"
        "\"internalHeapFragmentationPermille\":%u,"
        "\"heap\":\"blocks allocated/free %lu/%lu frag %u/1000; "
        "HTTP %ld/%ld "
        "WiFi %ld/%ld OTA %ld/%ld webhooks %ld/%ld "
        "Micra TLS %ld/%ld\","
        "\"psramSizeBytes\":%lu,\"psramFreeBytes\":%lu,"
        "\"psramMinimumFreeBytes\":%lu,"
        "\"psramLargestFreeBlockBytes\":%lu,"
        "\"bleHostAllocPsram\":%lu,\"bleHostAllocFallback\":%lu,"
        "\"hciRxDropped\":%lu,\"hciTxDropped\":%lu,"
        "\"bleBackend\":\"%s\",\"bleRuntimeState\":%u,"
        "\"bleRuntimeLastError\":%ld,\"bleRuntimeLastResetReason\":%ld,"
        "\"bleRuntimeSyncGeneration\":%lu,\"bleRuntimeResetCount\":%lu,"
        "\"bleRuntimeHostStackMinWords\":%lu,"
        "\"workBufExternal\":%s,\"jsonArenaExternal\":%s,"
        "\"allocExternalFallback\":%lu,"
        "\"hwmon\":{\"cpuLoad5s\":%.2f,\"cpuLoad1m\":%.2f,\"cpuLoad5m\":%.2f,"
        "\"cpu0Busy\":%.2f,\"cpu1Busy\":%.2f,\"cpuLoadValid\":%s,"
        "\"cpuMhz\":%lu,"
        "\"tempValid\":%s,\"tempC\":%.1f,\"tempPeakC\":%.1f,"
        "\"ramTotalBytes\":%lu,\"ramUsedBytes\":%lu,"
        "\"ramFreeBytes\":%lu}},"
        "\"safety\":{\"state\":\"%s\",\"fault\":\"%s\","
        "\"taskWatchdogReady\":%s,\"externalHardware\":%s,"
        "\"recoveryRequired\":%s,\"resetReasonCode\":%lu},"
        "\"scale\":{\"preferredMac\":\"%s\",\"preferredName\":\"%s\","
        "\"preferredFriendlyName\":\"%s\","
        "\"connectedMac\":\"%s\",\"connectedFriendlyName\":\"%s\","
        "\"streamState\":\"%s\",\"controlState\":\"%s\","
        "\"packetGaps\":%lu,\"maxPacketGapMs\":%lu,\"rejectedPackets\":%lu,"
        "\"reconnects\":%lu,\"lastDisconnectReasonName\":\"%s\","
        "\"lastDisconnect\":{\"summary\":\"%s\",\"sequence\":%lu,\"atMs\":%lu,\"ageMs\":%lu,"
        "\"generation\":%lu,\"reason\":\"%s\",\"origin\":\"%s\","
        "\"status\":%ld,\"domain\":\"%s\",\"teardownStatus\":%ld,"
        "\"command\":\"%s\",\"commandElapsedMs\":%lu},"
        "\"lastCommandFailure\":{\"summary\":\"%s\",\"sequence\":%lu,\"atMs\":%lu,"
        "\"command\":\"%s\",\"status\":%ld,\"elapsedMs\":%lu},"
        "\"eventsDropped\":%lu,\"recoveredStaleCount\":%lu,"
        "\"recoveredStaleMs\":%lu,\"rssi\":%s,"
        "\"observedWeightG\":%s,"
        "\"weightUpdateIntervalMs\":%s,\"timerMs\":%s,\"model\":\"%s\","
        "\"supportedCommandsKnown\":%s,\"supportedCommands\":%s},"
        "\"lastCommand\":{\"requestId\":%lu,\"state\":\"%s\"},"
        "\"compileFlags\":{\"buzzer\":\"%s\",\"remoteMachineControl\":%s,"
        "\"arch\":\"%s\",\"hardwareProfile\":\"%s\","
        "\"machineProfile\":\"%s\",\"machineBrand\":\"%s\","
        "\"machineModel\":\"%s\",\"machineType\":\"%s\",\"stopPulseMs\":%lu,"
        "\"maxSinglePressMs\":%lu,\"development\":%s},"
        "\"serial\":{\"io4\":\"%s\",\"state\":\"%s\"},"
        "\"boot\":{\"state\":\"%s\",\"complete\":%s,\"degraded\":%s,"
        "\"capabilities\":{\"platformClock\":%s,\"relaySafetyTimers\":%s,"
        "\"taskWatchdog\":%s,\"persistentStorage\":%s,\"settingsLoaded\":%s,"
        "\"settingsPersistenceWorker\":%s,\"scaleWorker\":%s,"
        "\"webCommandQueue\":%s,\"network\":%s,\"psram\":%s,"
        "\"criticalFaultLatched\":%s}}",
        stopperStateName(control.state),
        machineRunStateName(control.machineRunState),
        control.relayClosed ? "true" : "false",
        control.machineRunning ? "true" : "false",
        control.machineStartAckPending ? "true" : "false",
        control.machineStopAckPending ? "true" : "false",
        control.machineOrphanRun ? "true" : "false",
        control.reedOn ? "true" : "false",
        control.physicalActivatorOn ? "true" : "false",
        controlSourceName(control.source),
        control.scaleAvailable ? cupPresenceStateName(control.cupPresenceState) : "UNKNOWN",
        control.cupPresent ? "true" : "false",
        cupWeight, cupWeightValid ? "true" : "false",
        static_cast<unsigned long>(control.cupTare.placementId), idleTare,
        network.apActive ? "true" : "false", safeApSsid, network.apIp,
        static_cast<unsigned>(network.apClients),
        network.wifiConfigured ? "true" : "false", safeStaSsid,
        staStateName(network.staState), wifiPsLiveName(network.wifiPs),
        rfCoexPreferenceName(network.wifiCoex),
        staChannelJson, network.staIp,
        staIpModeName(network.staIpMode),
        staConfigStateName(network.staConfigState),
        static_cast<unsigned long>(network.confirmRemainingMs), staRssiJson,
        staSignalQualityJson, timeSyncStateName(timeStatus.state),
        static_cast<unsigned long>(timeStatus.utcSec),
        static_cast<unsigned long>(timeStatus.lastSyncAgeMs),
        static_cast<unsigned long>(timeStatus.lastSyncUtcSec),
        static_cast<unsigned long>(timeStatus.nextRetryInMs), safeActiveServer,
        control.maintenanceLeaseActive ? "true" : "false",
        static_cast<unsigned long>(control.maintenanceLeaseId),
        control.configPersistPending ? "true" : "false",
        control.configPersistFailed ? "true" : "false",
        static_cast<unsigned long>(control.loopStackMinBytes),
        static_cast<unsigned long>(control.scaleStackMinBytes),
        static_cast<unsigned long>(control.uptimeMs),
        static_cast<unsigned long>(control.snapshotVersion),
        control.snapshotStale ? "true" : "false",
        static_cast<unsigned long>(control.loopIntervalGapMs),
        static_cast<unsigned long>(control.loopMaxGapMs),
        static_cast<unsigned long>(control.loopDeadlineMisses),
        static_cast<unsigned long>(control.loopMaxExecutionUs),
        static_cast<unsigned long>(control.scaleWorkerMaxGapMs),
        static_cast<unsigned long>(control.scaleWorkerDeadlineMisses),
        static_cast<unsigned long>(control.scaleWorkerMaxExecutionUs),
        static_cast<unsigned long>(control.healthSnapshotVersion),
        static_cast<unsigned long>(control.healthSnapshotAgeMs),
        control.healthSnapshotValid ? "true" : "false",
        static_cast<unsigned long>(control.freeHeapBytes),
        static_cast<unsigned long>(control.minimumFreeHeapBytes),
        static_cast<unsigned long>(control.largestFreeHeapBlockBytes),
        static_cast<unsigned long>(control.internalHeapAllocatedBlocks),
        static_cast<unsigned long>(control.internalHeapFreeBlocks),
        static_cast<unsigned long>(control.internalHeapAllocatedBlocks +
                                   control.internalHeapFreeBlocks),
        static_cast<unsigned>(control.internalHeapFragmentationPermille),
        static_cast<unsigned long>(control.internalHeapAllocatedBlocks),
        static_cast<unsigned long>(control.internalHeapFreeBlocks),
        static_cast<unsigned>(control.internalHeapFragmentationPermille),
        static_cast<long>(networkHeap.http.lastDelta.largestBlock),
        static_cast<long>(networkHeap.http.worstLargestDelta),
        static_cast<long>(networkHeap.wifi.lastDelta.largestBlock),
        static_cast<long>(networkHeap.wifi.worstLargestDelta),
        static_cast<long>(networkHeap.ota.lastDelta.largestBlock),
        static_cast<long>(networkHeap.ota.worstLargestDelta),
        static_cast<long>(webhookHeap.lastDelta.largestBlock),
        static_cast<long>(webhookHeap.worstLargestDelta),
        static_cast<long>(micraHeap.lastDelta.largestBlock),
        static_cast<long>(micraHeap.worstLargestDelta),
        static_cast<unsigned long>(control.psramSizeBytes),
        static_cast<unsigned long>(control.psramFreeBytes),
        static_cast<unsigned long>(control.psramMinimumFreeBytes),
        static_cast<unsigned long>(control.psramLargestFreeBlockBytes),
        static_cast<unsigned long>(control.bleHostAllocPsramCount),
        static_cast<unsigned long>(control.bleHostAllocFallbackCount),
        static_cast<unsigned long>(control.bleHostHciRxDropped),
        static_cast<unsigned long>(control.bleHostHciTxDropped),
        "nimble",
        static_cast<unsigned>(control.bleRuntimeState),
        static_cast<long>(control.bleRuntimeLastError),
        static_cast<long>(control.bleRuntimeLastResetReason),
        static_cast<unsigned long>(control.bleRuntimeSyncGeneration),
        static_cast<unsigned long>(control.bleRuntimeResetCount),
        static_cast<unsigned long>(control.bleRuntimeHostStackMinBytes),
        control.workBufExternal ? "true" : "false",
        control.jsonArenaExternal ? "true" : "false",
        static_cast<unsigned long>(control.allocExternalFallbackCount),
        static_cast<double>(control.hwmon.cpuLoad5s),
        static_cast<double>(control.hwmon.cpuLoad1m),
        static_cast<double>(control.hwmon.cpuLoad5m),
        static_cast<double>(control.hwmon.cpu0Busy),
        static_cast<double>(control.hwmon.cpu1Busy),
        control.hwmon.cpuLoadValid ? "true" : "false",
        static_cast<unsigned long>(control.hwmon.cpuMhz),
        control.hwmon.tempValid ? "true" : "false",
        static_cast<double>(control.hwmon.tempC),
        static_cast<double>(control.hwmon.tempPeakC),
        static_cast<unsigned long>(control.hwmon.ramTotalBytes),
        static_cast<unsigned long>(control.hwmon.ramUsedBytes),
        static_cast<unsigned long>(control.hwmon.ramFreeBytes),
        relaySafetyStateName(control.safetyState),
        relaySafetyFaultName(control.safetyFault),
        control.taskWatchdogReady ? "true" : "false",
        control.externalSafetyPresent ? "true" : "false",
        control.resetRecoveryRequired ? "true" : "false",
        static_cast<unsigned long>(control.resetReasonCode),
        safePreferredScaleMac, safePreferredScaleName,
        safePreferredScaleFriendly, safeConnectedScaleMac,
        safeConnectedScaleFriendly,
        weightStreamStateName(control.weightStreamState),
        weightControlStateName(control.weightControlState),
        static_cast<unsigned long>(control.scalePacketGaps),
        static_cast<unsigned long>(control.scaleMaxPacketGapMs),
        static_cast<unsigned long>(control.scaleRejectedPackets),
        static_cast<unsigned long>(control.scaleReconnects),
        scaleDisconnectReasonName(control.scaleLastDisconnectReason),
        disconnectSummary,
        static_cast<unsigned long>(ble.disconnectSequence),
        static_cast<unsigned long>(ble.disconnectAtMs),
        static_cast<unsigned long>(ble.disconnectSequence == 0 ? 0 : millis() - ble.disconnectAtMs),
        static_cast<unsigned long>(ble.disconnectGeneration),
        scaleDisconnectReasonName(ble.disconnectReason),
        scaleBleOriginName(ble.disconnectOrigin),
        static_cast<long>(ble.disconnectStatus),
        scaleBleStatusDomain(ble.disconnectStatus, ble.disconnectOrigin),
        static_cast<long>(ble.teardownStatus),
        scaleBleCommandName(ble.disconnectCommand),
        static_cast<unsigned long>(ble.disconnectCommandElapsedMs),
        commandSummary,
        static_cast<unsigned long>(ble.commandFailureSequence),
        static_cast<unsigned long>(ble.commandFailureAtMs),
        scaleBleCommandName(ble.command),
        static_cast<long>(ble.commandStatus),
        static_cast<unsigned long>(ble.commandElapsedMs),
        static_cast<unsigned long>(control.scaleEventsDropped),
        static_cast<unsigned long>(control.scaleRecoveredStaleCount),
        static_cast<unsigned long>(control.scaleRecoveredStaleMs),
        scaleRssiJson,
        scaleObservedWeightJson,
        scaleWeightUpdateIntervalJson,
        scaleTimerJson,
        scaleModelName(control.scaleModel),
        control.scaleModel == ScaleModel::Unknown ? "false" : "true",
        supportedCommandsJson,
        static_cast<unsigned long>(network.lastCommandRequestId),
        commandResultStateName(network.lastCommandState),
        compiledBuzzerModeId(),
        REMOTE_MACHINE_CONTROL_ENABLED ? "true" : "false",
        FW_BOARD_ARCH_STRING,
        SHOT_STOPPER_HARDWARE_PROFILE_ID,
        SHOT_STOPPER_MACHINE_PROFILE_ID,
        SHOT_STOPPER_MACHINE_BRAND,
        SHOT_STOPPER_MACHINE_MODEL,
        compiledMachineTypeId(),
        static_cast<unsigned long>(COMPILED_STOP_PULSE_MS),
        static_cast<unsigned long>(COMPILED_MAX_SINGLE_PRESS_MS),
        DEVELOPMENT_BUILD ? "true" : "false",
        usbConsoleIo4StateId(control.usbConsoleIo4Closed),
        usbSerialStateId(control.usbSerialEnableSource),
        bootStateName(control.bootState),
        control.bootComplete ? "true" : "false",
        control.bootDegraded ? "true" : "false",
        control.bootCapabilities.platformClock ? "true" : "false",
        control.bootCapabilities.relaySafetyTimers ? "true" : "false",
        control.bootCapabilities.taskWatchdog ? "true" : "false",
        control.bootCapabilities.persistentStorage ? "true" : "false",
        control.bootCapabilities.settingsLoaded ? "true" : "false",
        control.bootCapabilities.settingsPersistenceWorker ? "true" : "false",
        control.bootCapabilities.scaleWorker ? "true" : "false",
        control.bootCapabilities.webCommandQueue ? "true" : "false",
        control.bootCapabilities.network ? "true" : "false",
        control.bootCapabilities.psram ? "true" : "false",
        control.bootCapabilities.criticalFaultLatched ? "true" : "false");
    if (ok) {
      ok = statusJsonAppend(
          &used,
          ",\"webhooks\":{\"workerReady\":%s,\"sending\":%s,"
          "\"workerStarts\":%lu,\"workerStops\":%lu,"
          "\"workerStartFailures\":%lu,\"clientCreates\":%lu,"
          "\"clientReuses\":%lu,\"transportResets\":%lu,"
          "\"clientCleanups\":%lu,\"heapSamples\":%lu,"
          "\"internalFreeBefore\":%lu,\"internalFreeAfter\":%lu,"
          "\"internalLargestBefore\":%lu,\"internalLargestAfter\":%lu,"
          "\"internalLargestMinimum\":%lu,\"psramLargestBefore\":%lu,"
          "\"psramLargestAfter\":%lu}",
          webhookStatus.workerReady ? "true" : "false",
          webhookStatus.sending ? "true" : "false",
          static_cast<unsigned long>(webhookStatus.workerStarts),
          static_cast<unsigned long>(webhookStatus.workerStops),
          static_cast<unsigned long>(webhookStatus.workerStartFailures),
          static_cast<unsigned long>(webhookStatus.clientCreates),
          static_cast<unsigned long>(webhookStatus.clientReuses),
          static_cast<unsigned long>(webhookStatus.transportResets),
          static_cast<unsigned long>(webhookStatus.clientCleanups),
          static_cast<unsigned long>(webhookStatus.heapSamples),
          static_cast<unsigned long>(webhookStatus.internalFreeBefore),
          static_cast<unsigned long>(webhookStatus.internalFreeAfter),
          static_cast<unsigned long>(webhookStatus.internalLargestBefore),
          static_cast<unsigned long>(webhookStatus.internalLargestAfter),
          static_cast<unsigned long>(webhookStatus.internalLargestMinimum),
          static_cast<unsigned long>(webhookStatus.psramLargestBefore),
          static_cast<unsigned long>(webhookStatus.psramLargestAfter));
    }
    if (ok) {
      ok = statusJsonAppend(&used, ",\"heapLifecycle\":{") &&
           appendHeapLifecycleJson(&used, "http", networkHeap.http, "") &&
           appendHeapLifecycleJson(&used, "wifi", networkHeap.wifi, ",") &&
           appendHeapLifecycleJson(&used, "ota", networkHeap.ota, ",") &&
           appendHeapLifecycleJson(&used, "webhookTls", webhookHeap, ",") &&
           appendHeapLifecycleJson(&used, "micraTls", micraHeap, ",") &&
           appendHeapLifecycleJson(&used, "micraWebsocket", micraWsHeap, ",") &&
           statusJsonAppend(&used, "}");
    }
    if (ok) {
      const NvsDiagnosticSnapshot nvs = captureNvsDiagnostics();
      ok = statusJsonAppend(
          &used,
          ",\"nvs\":{\"statsValid\":%s,\"statsErrorCode\":%ld,"
          "\"statsErrorName\":\"%s\",\"partitionBytes\":%lu,"
          "\"expectedPartitionBytes\":%lu,\"layoutExpected\":%s,"
          "\"usedEntries\":%lu,\"freeEntries\":%lu,"
          "\"availableEntries\":%lu,\"totalEntries\":%lu,"
          "\"namespaces\":%lu,\"failures\":%lu,"
          "\"flashIoLockTimeouts\":%lu,\"lastFailure\":",
          nvs.statsValid ? "true" : "false",
          static_cast<long>(nvs.statsErrorCode),
          nvsErrorName(nvs.statsErrorCode),
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
        ok = statusJsonAppend(
            &used,
            "{\"subsystem\":\"%s\",\"operation\":\"%s\","
            "\"errorCode\":%ld,\"errorName\":\"%s\","
            "\"requiredEntries\":%lu,\"availableEntriesAtFailure\":%lu}}",
            nvsSubsystemName(nvs.lastFailure.subsystem),
            nvsOperationName(nvs.lastFailure.operation),
            static_cast<long>(nvs.lastFailure.errorCode),
            nvsErrorName(nvs.lastFailure.errorCode),
            static_cast<unsigned long>(nvs.lastFailure.requiredEntries),
            static_cast<unsigned long>(
                nvs.lastFailure.availableEntriesAtFailure));
      } else if (ok) {
        ok = statusJsonAppend(&used, "null}");
      }
    }
    if (ok) {
      static constexpr const char *owners[] = {
          "other", "network", "webhook", "companion", "profiler",
          "flash_io", "ota", "buzzer", "json", "serial_log"};
      static_assert(sizeof(owners) / sizeof(owners[0]) ==
                        static_cast<size_t>(AllocationOwner::COUNT),
                    "Allocation owner names must match metrics");
      ok = statusJsonAppend(&used, ",\"memoryAllocations\":{");
      for (size_t i = 0; ok && i < static_cast<size_t>(AllocationOwner::COUNT); ++i) {
        const auto &metrics = detail::g_allocations[i];
        ok = statusJsonAppend(
            &used, "%s\"%s\":{\"successes\":%lu,\"failures\":%lu,"
            "\"largestRequestBytes\":%lu,\"lastFailureBytes\":%lu}",
            i ? "," : "", owners[i],
            static_cast<unsigned long>(metrics.successes.load(std::memory_order_relaxed)),
            static_cast<unsigned long>(metrics.failures.load(std::memory_order_relaxed)),
            static_cast<unsigned long>(metrics.largestRequestBytes.load(std::memory_order_relaxed)),
            static_cast<unsigned long>(metrics.lastFailureBytes.load(std::memory_order_relaxed)));
      }
      if (ok) ok = statusJsonAppend(&used, "}");
    }
    if (ok) ok = statusJsonAppend(&used, ",\"resetHistory\":[");
    for (uint8_t i = 0; ok && i < control.resetHistoryCount; ++i) {
      const ResetHistoryEntry &entry = control.resetHistory[i];
      ok = statusJsonAppend(
          &used, "%s{\"reason\":\"%s\",\"uptimeMs\":%lu}",
          i ? "," : "", safetyResetReasonName(entry.reasonCode),
          static_cast<unsigned long>(entry.uptimeMs));
    }
    if (ok) ok = statusJsonAppend(&used, "]");
    if (ok) {
      const bool bbwEnabled = !control.config.timerOnly;
      const bool scaleUsable =
          control.scaleAvailable &&
          control.weightStreamState == WeightStreamState::FRESH;
      const bool lastShotCupRemoved =
          control.lastShot.valid &&
          control.lastShot.endReason == EndReason::CUP_REMOVED;
      ok = statusJsonAppend(
          &used,
          ",\"guards\":{"
          "\"bbwEnabled\":%s,\"scaleUsable\":%s,\"lastShotCupRemoved\":%s,"
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
          "\"bbwProtectionEnded\":%s}}",
          bbwEnabled ? "true" : "false", scaleUsable ? "true" : "false",
          lastShotCupRemoved ? "true" : "false",
          control.noScaleShotGuardEnabled ? "true" : "false",
          control.noScaleShotGuardArmed ? "true" : "false",
          control.noScaleShotGuardHold ? "true" : "false",
          noScaleBbwModeId(control.config.noScaleBbwMode),
          static_cast<unsigned long>(
              control.noScaleShotGuardCooldownRemainingMs),
          control.noScaleShotGuardScaleWasAvailable ? "true" : "false",
          control.config.autoToManualGuardEnabled ? "true" : "false",
          control.cycleAutoToManualGuardArmed ? "true" : "false",
          control.cycleAutoToManualGuardEnforced ? "true" : "false",
          static_cast<unsigned long>(
              control.cycleAutoToManualGuardRemainingMs),
          control.config.slowExtractionGuardEnabled ? "true" : "false",
          control.cycleSlowExtractionExtended ? "true" : "false",
          control.cycleTargetReachedEarly ? "true" : "false",
          static_cast<double>(control.cycleActiveStopWeightG),
          control.config.fastExtractionGuardEnabled ? "true" : "false",
          control.cycleExtractionExtended ? "true" : "false",
          control.cycleTargetReachedEarly ? "true" : "false",
          static_cast<double>(control.cycleActiveStopWeightG),
          static_cast<unsigned long>(control.cycleMinBbwBrewTimeRemainingMs),
          control.config.avoidAccidentalTouchEnabled ? "true" : "false",
          control.cycleAccidentalTouchHolding ? "true" : "false",
          accidentalTouchPhaseName(static_cast<AccidentalTouchPhase>(
              control.cycleAccidentalTouchPhase)),
          accidentalTouchClassName(static_cast<AccidentalTouchClass>(
              control.cycleAccidentalTouchClass)),
          static_cast<unsigned>(control.cycleAccidentalTouchPendingCount),
          control.config.cupProtectionEnabled ? "true" : "false",
          control.config.stopIfCupRemoved ? "true" : "false",
          control.config.requireCupToStart ? "true" : "false",
          control.cupPresent ? "true" : "false",
          control.cupStartGuardHold ? "true" : "false",
          control.cycleCupRemovedPending ? "true" : "false",
          control.cycleBbwProtectionEnabled && !control.cycleBbwProtectionEnded
              ? "true"
              : "false",
          control.cycleBbwProtectionEnded ? "true" : "false");
    }
    if (ok) {
      if (self.callbacks_.copyTaskProfiler != nullptr) {
        self.callbacks_.copyTaskProfiler(self.workBuf_->taskProfiler);
      } else {
        self.workBuf_->taskProfiler = TaskProfilerSnapshot{};
      }
      ok = statusJsonAppendTaskProfiler(&used, self.workBuf_->taskProfiler);
    }
    if (ok) {
      ScaleProfilerStatus scaleProfile{};
      if (self.callbacks_.copyScaleProfilerStatus != nullptr) {
        self.callbacks_.copyScaleProfilerStatus(scaleProfile);
      }
      ok = statusJsonAppendScaleProfiler(&used, scaleProfile);
    }
    }
  }

  if (ok) {
    ok = statusJsonAppend(&used, "}");
  }

  if (!ok) {
    const esp_err_t tooLarge =
        sendError(request, "500 Internal Server Error", "STATUS_TOO_LARGE",
                  "Status snapshot exceeds its size limit.");
    self.unlockWorkBuf();
    return tooLarge;
  }
  const esp_err_t sent =
      sendJson(request, STATUS_OK, g_work->statusJson);
  self.unlockWorkBuf();
  return sent;
}

namespace {
