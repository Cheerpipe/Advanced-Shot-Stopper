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
  // Home/settings stay claim-gated only. Admin and Diagnostic mutations and
  // the full privileged status body require a temporary device-password unlock.
  self.requestPendingNetworkConfirm();

  const StatusPage page = parseStatusPage(request->uri);
  if (page == StatusPage::Unknown) {
    return sendError(request, "404 Not Found", "STATUS_PAGE_UNKNOWN",
                     "Unknown status page; use /api/v1/status/{home|settings|admin|diagnostic}.");
  }

  // Diagnostic is opt-in and, once enabled, intentionally has no Admin
  // authentication requirement: the page is fully functional for every
  // visitor while visible. Only the Admin-only toggle itself stays locked.
  if (page == StatusPage::Diagnostic && !self.diagnosticPageEnabled()) {
    return sendError(request, STATUS_FORBIDDEN, "DIAGNOSTIC_DISABLED",
                     "Diagnostic page is disabled by the administrator.");
  }

  // JSON / snapshot work lives in one PSRAM-only blob.
  if (!self.lockWorkBufForStatus()) {
    return self.workBufBusy(request);
  }

  ControlStatusSnapshot &control = self.workBuf_->control;
  self.loadControlStatus(control);
  const PersistedLastShot &homeLastShot = control.lastGoodShot;
  const bool homeLastShotValid =
      page == StatusPage::Home && publishableLastShot(homeLastShot);
  const bool homeHistoryLinked =
      homeLastShotValid && !control.activeCycle &&
      control.lastGoodShotHistoryLinked;
  const bool configMutable = controlAllowsConfiguration(control);
  const bool webUiOverrideActive = self.webUiOverrideAllowed(request);
  const uint32_t webUiOverrideRemainingMs =
      webUiOverrideActive ? self.webUiOverrideRemainingMs(request) : 0;
  bool adminUnlocked = false;
  if (page == StatusPage::Admin || page == StatusPage::Home ||
      page == StatusPage::Diagnostic) {
    adminUnlocked = self.adminUnlockAllowed(request);
    if (adminUnlocked &&
        (page == StatusPage::Admin || page == StatusPage::Diagnostic)) {
      self.touchAdminUnlock();
    }
  }
  const NetworkStatusSnapshot network = self.snapshot();
  const TimeStatusSnapshot timeStatus = g_wallClock.snapshot(millis());
#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD
  LineaMicraStatus micraStatus = {};
  LineaMicraDiscoverySnapshot micraDiscovery = {};
  char micraSerial[LINEA_MICRA_SERIAL_CAPACITY] = {};
  char safeMicraSerial[LINEA_MICRA_SERIAL_CAPACITY] = {};
  char micraName[LINEA_MICRA_NAME_CAPACITY] = {};
  char safeMicraName[LINEA_MICRA_NAME_CAPACITY] = {};
  uint8_t micraOptions = 0;
  bool micraAccountConfigured = false;
  self.dataMux_.lock();
  micraAccountConfigured = self.settings_.lineaMicra.accountConfigured;
  micraOptions = self.settings_.lineaMicra.options;
  memcpy(micraSerial, self.settings_.lineaMicra.selectedSerial,
         sizeof(micraSerial));
  memcpy(micraName, self.settings_.lineaMicra.selectedName,
         sizeof(micraName));
  self.dataMux_.unlock();
  micraStatus = machineIntegrationStatus();
  micraDiscovery = machineIntegrationDiscovery();
  sanitizeJsonEmbed(micraSerial, safeMicraSerial, sizeof(safeMicraSerial));
  sanitizeJsonEmbed(micraName, safeMicraName, sizeof(safeMicraName));
#endif

  char currentWeight[32] = "null";
  char cupWeight[32] = "null";
  const bool cupWeightValid = control.cupPresent && control.cupTare.weightValid &&
                             std::isfinite(control.cupTare.weightG);
  if (cupWeightValid)
    snprintf(cupWeight, sizeof(cupWeight), "%.1f",
             static_cast<double>(control.cupTare.weightG));
  char observedWeight[32] = "null";
  char lastShotWeight[32] = "null";
  char scaleTimer[32] = "null";
  char staRssiJson[16] = "null";
  char staSignalQualityJson[16] = "null";
  char staChannelJson[8] = "null";
  char scaleRssiJson[16] = "null";
  char scaleWeightUpdateIntervalJson[16] = "null";
  if (control.currentWeightValid) {
    snprintf(currentWeight, sizeof(currentWeight), "%.2f",
             static_cast<double>(control.currentWeightG));
  }
  if (control.observedWeightValid) {
    snprintf(observedWeight, sizeof(observedWeight), "%.2f",
             static_cast<double>(control.observedWeightG));
  }
  if (control.currentTimerValid) {
    snprintf(scaleTimer, sizeof(scaleTimer), "%lu",
             static_cast<unsigned long>(control.currentTimerMs));
  }
  if (homeLastShotValid && homeLastShot.weightValid) {
    snprintf(lastShotWeight, sizeof(lastShotWeight), "%.2f",
             static_cast<double>(homeLastShot.currentWeightG));
  }
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

  char safeNtpCustom[NTP_SERVER_HOST_CAPACITY] = {};
  char safeActiveServer[NTP_SERVER_HOST_CAPACITY] = {};
  char safeScaleProtocol[24] = {};
  char safeHomeShotProtocol[24] = {};
  char safeHomeShotPreset[SHOT_PRESET_NAME_CAPACITY * 6] = {};
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
  sanitizeJsonEmbed(control.scaleProtocol, safeScaleProtocol,
                    sizeof(safeScaleProtocol));
  sanitizeJsonEmbed(homeLastShot.scaleProtocol, safeHomeShotProtocol,
                    sizeof(safeHomeShotProtocol));
  escapeJsonString(homeLastShot.presetName, safeHomeShotPreset,
                   sizeof(safeHomeShotPreset));
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

  const bool needPresets =
      page == StatusPage::Home || page == StatusPage::Settings;
  const bool needHistory = page == StatusPage::Settings;
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
  if (needHistory) {
    if (self.callbacks_.copyScaleHistory != nullptr) {
      self.callbacks_.copyScaleHistory(g_work->scaleHistory);
    } else {
      memset(g_work->scaleHistory, 0, sizeof(g_work->scaleHistory));
    }
    buildScaleHistoryJson(g_work->scaleHistory);
  } else {
    g_work->historyJson[0] = '[';
    g_work->historyJson[1] = ']';
    g_work->historyJson[2] = 0;
  }

  const uint32_t cycleFirstDropElapsedMs =
      control.cycleFirstDropMs != 0 && control.cycleStartedAtMs != 0 &&
              static_cast<int32_t>(control.cycleFirstDropMs -
                                   control.cycleStartedAtMs) >= 0
          ? control.cycleFirstDropMs - control.cycleStartedAtMs
          : 0U;

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
        &used, ",\"machineType\":\"%s\",\"machineIntegration\":\"%s\"",
        compiledMachineTypeId(),
#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD
        "linea_micra_cloud"
#else
        "none"
#endif
    );
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
  if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(&used, ",\"buzzerSupported\":%s",
                          BUZZER_SUPPORT_ENABLED ? "true" : "false");
  }
  if (ok) {
    ok = statusJsonAppend(
        &used, ",\"config\":{\"revision\":%lu,\"ringRetainLogLevel\":\"%s\",\"serialLogLevel\":\"%s\"",
        static_cast<unsigned long>(control.config.revision),
        logLevelName(static_cast<LogLevel>(control.config.ringRetainLogLevel)),
        logLevelName(serialLogLevelFromRuntime(control.config)));
  }
  if (ok && page == StatusPage::Admin && adminUnlocked) {
    ok = statusJsonAppend(
        &used,
        ",\"timezoneOffsetMinutes\":%d,\"ntpServerPreset\":\"%s\","
        "\"ntpServerCustom\":\"%s\",\"showDiagnosticPage\":%s,\"powerManagementEnabled\":%s,"
        "\"persistPending\":%s,\"persistFailed\":%s",
        static_cast<int>(control.config.timezoneOffsetMinutes),
        ntpPresetId(control.config.ntpServerPreset), safeNtpCustom,
        control.config.showDiagnosticPage ? "true" : "false",
        control.config.powerManagementEnabled ? "true" : "false",
        control.configPersistPending ? "true" : "false",
        control.configPersistFailed ? "true" : "false");
  }
  if (ok && page == StatusPage::Diagnostic) {
    ok = statusJsonAppend(
        &used,
        ",\"timezoneOffsetMinutes\":%d,\"serialDebugOutput\":%s",
        static_cast<int>(control.config.timezoneOffsetMinutes),
        serialLogLevelFromRuntime(control.config) != LogLevel::NONE ? "true"
                                                                     : "false");
  }

  if (ok && page == StatusPage::Home) {
    ok = statusJsonAppend(
        &used,
        ",\"soundAlertsEnabled\":%s,\"alertOutputChannel\":\"%s\","
        "\"brewByWeight\":%s,\"goalWeightG\":%u,"
        "\"operationalWallMs\":%lu,\"minBbwBrewTimeMs\":%lu,\"maxBbwBrewTimeMs\":%lu,"
        "\"minRecoveryWeightG\":%.1f,\"maxRecoveryWeightG\":%.1f,"
        "\"fastExtractionGuardEnabled\":%s,\"slowExtractionGuardEnabled\":%s,"
        "\"autoToManualGuardEnabled\":%s,\"cupProtectionEnabled\":%s,"
        "\"stopIfCupRemoved\":%s,\"requireCupToStart\":%s,"
        "\"avoidAccidentalTouchEnabled\":%s,"
        "\"cupPresentWeightG\":%.1f,\"cupRemovedWeightG\":%.1f,"
        "\"avoidBbwShotWithoutScale\":%s,\"noScaleBbwMode\":\"%s\","
        "\"scaleMacCacheMode\":\"%s\"",
        control.config.soundAlertsMuted ? "false" : "true",
        alertOutputChannelId(control.config.alertOutputChannel),
        control.config.timerOnly ? "false" : "true",
        static_cast<unsigned>(control.config.goalWeightG),
        static_cast<unsigned long>(control.config.operationalWallMs),
        static_cast<unsigned long>(control.config.minBbwBrewTimeMs),
        static_cast<unsigned long>(control.config.maxBbwBrewTimeMs),
        static_cast<double>(control.config.minRecoveryWeightG),
        static_cast<double>(control.config.maxRecoveryWeightG),
        control.config.fastExtractionGuardEnabled ? "true" : "false",
        control.config.slowExtractionGuardEnabled ? "true" : "false",
        control.config.autoToManualGuardEnabled ? "true" : "false",
        control.config.cupProtectionEnabled ? "true" : "false",
        control.config.stopIfCupRemoved ? "true" : "false",
        control.config.requireCupToStart ? "true" : "false",
        control.config.avoidAccidentalTouchEnabled ? "true" : "false",
        static_cast<double>(control.config.cupPresentWeightG),
        static_cast<double>(control.config.cupRemovedWeightG),
        noScaleBbwEnabled(control.config.noScaleBbwMode) ? "true" : "false",
        noScaleBbwModeId(control.config.noScaleBbwMode),
        scaleMacCacheModeId(control.config.scaleMacCacheMode));
  } else if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(
        &used,
        ",\"goalWeightG\":%u,\"weightOffsetG\":%.2f,"
        "\"weightOffsetBaselineG\":%.2f,\"autoTare\":%s,"
        "\"autoTareOutsideBrew\":%s,"
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

#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD
  if (ok && (page == StatusPage::Settings || page == StatusPage::Diagnostic)) {
    const uint32_t sampleAgeMs = micraStatus.sampleAtMs == 0
                                     ? 0
                                     : static_cast<uint32_t>(millis() -
                                                              micraStatus.sampleAtMs);
    const bool sampleValid = micraStatus.sampleAtMs != 0 &&
        (micraStatus.quality == LineaMicraObservationQuality::CURRENT ||
         micraStatus.quality == LineaMicraObservationQuality::STALE ||
         micraStatus.quality == LineaMicraObservationQuality::UNSUPPORTED);
    const bool sampleFresh =
        sampleValid && sampleAgeMs < micra_timing::kStateFreshnessMs;
    const LineaMicraPowerState reportedPower =
        sampleFresh ? micraStatus.powerState : LineaMicraPowerState::UNKNOWN;
    const LineaMicraObservationQuality reportedQuality =
        sampleValid && !sampleFresh ? LineaMicraObservationQuality::STALE
                                    : micraStatus.quality;
    ok = statusJsonAppend(
        &used,
        ",\"lineaMicra\":{\"accountConfigured\":%s,"
        "\"selectedSerial\":\"%s\",\"selectedName\":\"%s\","
        "\"applyTemperature\":%s,\"observeState\":%s,"
        "\"requestId\":%lu,\"phase\":\"%s\",\"error\":\"%s\","
        "\"powerState\":\"%s\","
        "\"observedMode\":\"%s\",\"quality\":\"%s\",\"effectiveOn\":%s,"
        "\"sampleValid\":%s,\"sampleAgeMs\":%lu,\"freshnessMs\":%lu,"
        "\"pollIntervalMs\":%lu,\"targetValid\":%s,\"targetDeciC\":%u,"
        "\"staConnected\":%s,\"apActive\":%s,\"shotPaused\":%s,"
        "\"httpStatus\":%u,\"transportStatus\":%ld,\"machines\":[",
        micraAccountConfigured ? "true" : "false", safeMicraSerial,
        safeMicraName,
        (micraOptions & LINEA_MICRA_APPLY_TEMPERATURE) != 0
            ? "true" : "false",
        (micraOptions & LINEA_MICRA_OBSERVE_STATE) != 0
            ? "true" : "false",
        static_cast<unsigned long>(micraStatus.requestId),
        lineaMicraPhaseName(micraStatus.phase),
        lineaMicraErrorName(micraStatus.error),
        lineaMicraPowerStateName(reportedPower),
        lineaMicraObservedModeName(micraStatus.observedMode),
        lineaMicraObservationQualityName(reportedQuality),
        reportedPower != LineaMicraPowerState::OFF ? "true" : "false",
        sampleValid ? "true" : "false",
        static_cast<unsigned long>(sampleAgeMs),
        static_cast<unsigned long>(micra_timing::kStateFreshnessMs),
        static_cast<unsigned long>(micra_timing::kStatePollMs),
        micraStatus.targetValid ? "true" : "false",
        static_cast<unsigned>(micraStatus.targetDeciC),
        micraStatus.staConnected ? "true" : "false",
        micraStatus.apActive ? "true" : "false",
        micraStatus.shotPaused ? "true" : "false",
        static_cast<unsigned>(micraStatus.httpStatus),
        static_cast<long>(micraStatus.transportStatus));
    for (uint8_t index = 0; ok && index < micraDiscovery.count; ++index) {
      char safeSerial[LINEA_MICRA_SERIAL_CAPACITY] = {};
      char safeName[LINEA_MICRA_NAME_CAPACITY] = {};
      sanitizeJsonEmbed(micraDiscovery.machines[index].serial, safeSerial,
                        sizeof(safeSerial));
      sanitizeJsonEmbed(micraDiscovery.machines[index].name, safeName,
                        sizeof(safeName));
      ok = statusJsonAppend(&used,
                            "%s{\"serial\":\"%s\",\"name\":\"%s\"}",
                            index == 0 ? "" : ",", safeSerial, safeName);
    }
    if (ok) ok = statusJsonAppend(&used, "]}");
  }
#endif

  if (ok && page == StatusPage::Home) {
    ok = statusJsonAppend(&used, ",\"adminUnlocked\":%s,\"development\":%s",
                          adminUnlocked ? "true" : "false",
                          DEVELOPMENT_BUILD ? "true" : "false");
  }
  if (ok && page == StatusPage::Home) {
    ok = statusJsonAppend(
        &used,
        ",\"state\":\"%s\",\"stateLabel\":\"%s\",\"machineState\":\"%s\","
        "\"relayClosed\":%s,\"machineRunning\":%s,"
        "\"physicalActivatorOn\":%s,\"virtualHoldOn\":%s,"
        "\"remoteControlEnabled\":%s,\"controlSource\":\"%s\","
        "\"circuitElapsedMs\":%lu,"
        "\"safety\":{\"state\":\"%s\",\"fault\":\"%s\","
        "\"taskWatchdogReady\":%s,\"externalHardware\":%s,"
        "\"recoveryRequired\":%s},"
        "\"scale\":{\"available\":%s,\"protocol\":\"%s\","
        "\"streamState\":\"%s\",\"controlState\":\"%s\","
        "\"controlAccepted\":%s,\"currentWeightG\":%s,"
        "\"observedWeightG\":%s,\"timerMs\":%s,"
        "\"preferredMac\":\"%s\",\"preferredName\":\"%s\","
        "\"macCachePauseRemainingMs\":%lu,"
        "\"lastDisconnectReasonName\":\"%s\"},"
        "\"presets\":%s,"
        "\"cycle\":{\"active\":%s,\"shotType\":\"%s\","
        "\"retarePerformed\":%s,\"firstDropElapsedMs\":%lu,"
        "\"extractionExtended\":%s,\"slowExtractionExtended\":%s,"
        "\"activeStopWeightG\":%.1f,\"minBbwBrewTimeRemainingMs\":%lu,"
        "\"autoToManualGuardArmed\":%s,"
        "\"autoToManualGuardEnforced\":%s,"
        "\"autoToManualGuardRemainingMs\":%lu,"
        "\"accidentalTouchHolding\":%s},"
        "\"lastShot\":{\"valid\":%s,\"currentWeightG\":%s,"
        "\"cycleId\":%lu,\"presetId\":%u,\"presetName\":\"%s\","
        "\"goalWeightG\":%u,\"averageFlowGps\":%.2f,"
        "\"averageFlowValid\":%s,\"extractionExtended\":%s,"
        "\"activeStopWeightG\":%.1f,\"durationMs\":%lu,"
        "\"firstDropElapsedMs\":%lu,\"retarePerformed\":%s,"
        "\"shotType\":\"%s\",\"scaleProtocol\":\"%s\","
        "\"scaleAvailable\":%s,\"fastExtractionGuardEnabled\":%s,"
        "\"slowExtractionGuardEnabled\":%s,\"slowExtractionExtended\":%s,"
        "\"minBbwBrewTimeRemainingMs\":%lu,"
        "\"autoToManualGuardEnabled\":%s,"
        "\"autoToManualGuardArmed\":%s,"
        "\"autoToManualGuardEnforced\":%s,"
        "\"autoToManualGuardRemainingMs\":%lu,"
        "\"noScaleShotGuardEnabled\":%s,"
        "\"noScaleShotGuardArmed\":%s,\"noScaleBbwMode\":\"%s\","
        "\"endReason\":\"%s\","
        "\"rating\":%u,\"shotLogId\":%lu},"
        "\"noScaleShotGuard\":{\"enabled\":%s,\"armed\":%s,"
        "\"mode\":\"%s\",\"cooldownRemainingMs\":%lu,"
        "\"scaleUsable\":%s},"
        "\"cupPresence\":{\"state\":\"%s\",\"present\":%s,"
        "\"weightG\":%s,\"weightValid\":%s,\"placementId\":%lu}",
        stopperStateName(control.state), stateLabel(control.state),
        machineRunStateName(control.machineRunState),
        control.relayClosed ? "true" : "false",
        control.machineRunning ? "true" : "false",
        control.physicalActivatorOn ? "true" : "false",
        control.virtualHoldOn ? "true" : "false",
        control.remoteControlEnabled ? "true" : "false",
        controlSourceName(control.source),
        static_cast<unsigned long>(control.circuitElapsedMs),
        relaySafetyStateName(control.safetyState),
        relaySafetyFaultName(control.safetyFault),
        control.taskWatchdogReady ? "true" : "false",
        control.externalSafetyPresent ? "true" : "false",
        control.resetRecoveryRequired ? "true" : "false",
        control.scaleAvailable ? "true" : "false", safeScaleProtocol,
        weightStreamStateName(control.weightStreamState),
        weightControlStateName(control.weightControlState),
        control.currentWeightValid ? "true" : "false", currentWeight,
        observedWeight, scaleTimer, safePreferredScaleMac,
        safePreferredScaleName,
        static_cast<unsigned long>(control.scaleMacCachePauseRemainingMs),
        scaleDisconnectReasonName(control.scaleLastDisconnectReason),
        g_work->presetsJson, control.activeCycle ? "true" : "false",
        activeCycleShotTypeLabel(control),
        control.cycleRetarePerformed ? "true" : "false",
        static_cast<unsigned long>(cycleFirstDropElapsedMs),
        control.cycleExtractionExtended ? "true" : "false",
        control.cycleSlowExtractionExtended ? "true" : "false",
        static_cast<double>(control.cycleActiveStopWeightG),
        static_cast<unsigned long>(control.cycleMinBbwBrewTimeRemainingMs),
        control.cycleAutoToManualGuardArmed ? "true" : "false",
        control.cycleAutoToManualGuardEnforced ? "true" : "false",
        static_cast<unsigned long>(control.cycleAutoToManualGuardRemainingMs),
        control.cycleAccidentalTouchHolding ? "true" : "false",
        homeLastShotValid ? "true" : "false", lastShotWeight,
        static_cast<unsigned long>(homeLastShot.cycleId),
        static_cast<unsigned>(homeLastShot.presetId), safeHomeShotPreset,
        static_cast<unsigned>(homeLastShot.goalWeightG),
        static_cast<double>(homeLastShot.averageFlowGps),
        homeLastShot.averageFlowValid ? "true" : "false",
        homeLastShot.extractionExtended ? "true" : "false",
        static_cast<double>(homeLastShot.activeStopWeightG),
        static_cast<unsigned long>(homeLastShot.durationMs),
        static_cast<unsigned long>(homeLastShot.firstDropElapsedMs),
        homeLastShot.retarePerformed ? "true" : "false",
        homeLastShotValid
            ? lastShotTypeName(static_cast<LastShotType>(homeLastShot.shotType))
            : "unknown",
        safeHomeShotProtocol,
        homeLastShot.scaleAvailable ? "true" : "false",
        homeLastShot.fastExtractionGuardEnabled ? "true" : "false",
        homeLastShot.slowExtractionGuardEnabled ? "true" : "false",
        homeLastShot.slowExtractionExtended ? "true" : "false",
        static_cast<unsigned long>(homeLastShot.minBbwBrewTimeRemainingMs),
        homeLastShot.autoToManualGuardEnabled ? "true" : "false",
        homeLastShot.autoToManualGuardArmed ? "true" : "false",
        homeLastShot.autoToManualGuardEnforced ? "true" : "false",
        static_cast<unsigned long>(homeLastShot.autoToManualGuardRemainingMs),
        homeLastShot.noScaleShotGuardEnabled ? "true" : "false",
        homeLastShot.noScaleShotGuardArmed ? "true" : "false",
        noScaleBbwModeId(homeLastShot.noScaleBbwMode),
        homeLastShotValid
            ? shotLogStopDetailName(
                  shotLogStopDetailFromEndReason(
                      homeLastShot.endReason,
                      homeLastShot.fastExtractionGuardEnabled,
                      homeLastShot.extractionExtended))
            : "none",
        homeHistoryLinked ? static_cast<unsigned>(homeLastShot.rating) : 0U,
        homeHistoryLinked
            ? static_cast<unsigned long>(homeLastShot.shotLogId)
            : 0UL,
        control.noScaleShotGuardEnabled ? "true" : "false",
        control.noScaleShotGuardArmed ? "true" : "false",
        noScaleBbwModeId(control.config.noScaleBbwMode),
        static_cast<unsigned long>(
            control.noScaleShotGuardCooldownRemainingMs),
        (control.scaleAvailable &&
         control.weightStreamState == WeightStreamState::FRESH)
            ? "true"
            : "false",
        cupPresenceStateName(control.cupPresenceState),
        control.cupPresent ? "true" : "false", cupWeight,
        cupWeightValid ? "true" : "false",
        static_cast<unsigned long>(control.cupTare.placementId));
    if (ok) {
      const ShotCurveRecord curve = shotCurveRecordFromStatusFields(
          control.shotCurveCount, control.shotCurveIntervalDs,
          control.shotCurveFirstDropDs, control.shotCurveFirstDropCg,
          control.shotCurveExtendedDs, control.shotCurveExtendedCg,
          control.shotCurveAtmDs, control.shotCurveAtmCg,
          control.shotCurveAtmClearedDs, control.shotCurveEndedDs,
          control.shotCurveEndedCg, control.shotCurveWeightCg,
          sizeof(control.shotCurveWeightCg) /
              sizeof(control.shotCurveWeightCg[0]));
      char *const curveJson = g_work->curveJson;
      if (formatShotCurveJsonBody(curveJson, SHOT_CURVE_JSON_CAPACITY,
                                  curve)) {
        ok = statusJsonAppend(&used, ",\"shotCurve\":{%s}", curveJson);
      }
    }
  } else if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(
        &used,
        ",\"scale\":{\"preferredMac\":\"%s\",\"preferredName\":\"%s\","
        "\"macCachePauseRemainingMs\":%lu,\"history\":%s},"
        "\"presets\":%s,\"stopPulseMs\":%lu,\"maxSinglePressMs\":%lu,"
        "\"momentaryStartEdge\":\"%s\",\"reedConfirmTimeoutMs\":%lu,"
        "\"assumeIdleWhenScaleConnects\":%s,\"shotReactTimeoutS\":%u",
        safePreferredScaleMac, safePreferredScaleName,
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
          "\"bleScan\":{\"scanIntensity\":\"%s\",\"backoffMin\":%u,"
          "\"boostMin\":%u}",
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
          static_cast<unsigned>(control.bleScanBackoffMin),
          static_cast<unsigned>(control.bleScanBoostMin));
      if (ok) {
        ok = statusJsonAppend(
            &used,
            ",\"webhooks\":{\"enabled\":%s,\"url\":\"%s\","
            "\"brewState\":%s,\"firstDrop\":%s,\"end\":%s,"
            "\"deferDuringShot\":%s,"
            "\"workerReady\":%s,\"sending\":%s,\"lastSuccess\":%s,"
            "\"lastHttpStatus\":%u,\"lastError\":%ld,"
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
    // Reading the diagnostic page is public while it is enabled; adminUnlocked
    // reflects the caller's real Admin session (controls chrome on the UI).
    ok = statusJsonAppend(&used, ",\"adminUnlocked\":%s,\"diagnosticPublic\":true,\"development\":%s",
                          adminUnlocked ? "true" : "false",
                          DEVELOPMENT_BUILD ? "true" : "false");
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
    ok = statusJsonAppend(
        &used,
        ",\"state\":\"%s\",\"machineState\":\"%s\","
        "\"relayClosed\":%s,\"machineRunning\":%s,"
        "\"machineStartAck\":%s,\"machineStopAck\":%s,\"machineOrphan\":%s,"
        "\"reedOn\":%s,"
        "\"physicalActivatorOn\":%s,"
        "\"controlSource\":\"%s\","
        "\"cupPresence\":{\"state\":\"%s\",\"present\":%s,"
        "\"weightG\":%s,\"weightValid\":%s,\"placementId\":%lu},"
        "\"network\":{\"apActive\":%s,\"apSsid\":\"%s\",\"apIp\":\"%s\",\"apClients\":%u,"
        "\"wifiConfigured\":%s,\"ssid\":\"%s\","
        "\"staState\":\"%s\",\"wifiPs\":\"%s\",\"wifiCoex\":\"%s\",\"channel\":%s,\"staIp\":\"%s\",\"ipMode\":\"%s\","
        "\"configState\":\"%s\",\"confirmRemainingMs\":%lu,"
        "\"rssi\":%s,\"signalQualityPct\":%s},"
        "\"time\":{\"state\":\"%s\",\"utcSec\":%lu,\"lastSyncAgeMs\":%lu,"
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
        "\"freeHeapBytes\":%lu,\"minimumFreeHeapBytes\":%lu,"
        "\"largestFreeHeapBlockBytes\":%lu,"
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
        "\"scale\":{\"streamState\":\"%s\",\"controlState\":\"%s\","
        "\"packetGaps\":%lu,\"rejectedPackets\":%lu,"
        "\"reconnects\":%lu,\"lastDisconnectReasonName\":\"%s\","
        "\"lastDisconnect\":{\"summary\":\"%s\",\"sequence\":%lu,\"atMs\":%lu,\"ageMs\":%lu,"
        "\"generation\":%lu,\"reason\":\"%s\",\"origin\":\"%s\","
        "\"status\":%ld,\"domain\":\"%s\",\"teardownStatus\":%ld,"
        "\"command\":\"%s\",\"commandElapsedMs\":%lu},"
        "\"lastCommandFailure\":{\"summary\":\"%s\",\"sequence\":%lu,\"atMs\":%lu,"
        "\"command\":\"%s\",\"status\":%ld,\"elapsedMs\":%lu},"
        "\"eventsDropped\":%lu,\"recoveredStaleCount\":%lu,"
        "\"recoveredStaleMs\":%lu,\"rssi\":%s,"
        "\"weightUpdateIntervalMs\":%s},"
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
        cupPresenceStateName(control.cupPresenceState),
        control.cupPresent ? "true" : "false",
        cupWeight, cupWeightValid ? "true" : "false",
        static_cast<unsigned long>(control.cupTare.placementId),
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
        static_cast<unsigned long>(control.freeHeapBytes),
        static_cast<unsigned long>(control.minimumFreeHeapBytes),
        static_cast<unsigned long>(control.largestFreeHeapBlockBytes),
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
        weightStreamStateName(control.weightStreamState),
        weightControlStateName(control.weightControlState),
        static_cast<unsigned long>(control.scalePacketGaps),
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
        scaleWeightUpdateIntervalJson,
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
          "flash_io", "ota", "buzzer", "json"};
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
