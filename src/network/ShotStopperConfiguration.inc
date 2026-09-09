esp_err_t ShotStopperNetwork::timeSyncHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical activator OFF, and wait for Ready before syncing the clock.");
  }
  self.ntpManualSyncPending_ = true;
  return self.sendAccepted(request, self.allocateRequestId());
}
esp_err_t ShotStopperNetwork::configHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Configuration is locked while a cycle is active.");
  }
  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "A bounded JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  const bool dateTimePatch =
      jsonFieldPresent(root, "timezoneOffsetMinutes") ||
      jsonFieldPresent(root, "ntpServerPreset") ||
      jsonFieldPresent(root, "ntpServerCustom");
  // Log/debug fields are public while the diagnostic page is enabled; the
  // page visibility toggle itself always requires the Admin session.
  const bool diagnosticLogPatch = jsonFieldPresent(root, "serialLogLevel") ||
                                  jsonFieldPresent(root, "serialDebugOutput") ||
                                  jsonFieldPresent(root, "ringRetainLogLevel");
  const bool diagnosticPagePatch =
      jsonFieldPresent(root, "showDiagnosticPage") ||
      jsonFieldPresent(root, "powerManagementEnabled");
  // Patch: seed live effective config; only present keys overwrite.
  RuntimeConfig candidate = {};
  if (self.callbacks_.copyRuntimeConfig != nullptr) {
    self.callbacks_.copyRuntimeConfig(&candidate);
  }
  bool brewByWeightPresent = false;
  bool brewByWeight = !candidate.timerOnly;
  bool baseRevisionPresent = false;
  uint32_t baseRevision = 0;
  bool soundAlertsEnabled = !candidate.soundAlertsMuted;
  BullseyeMelodyConfig candidateBullseye = {};
  if (self.callbacks_.copyBullseyeConfig != nullptr) {
    self.callbacks_.copyBullseyeConfig(&candidateBullseye);
  } else {
    self.dataMux_.lock();
    candidateBullseye = self.settings_.bullseyeMelody;
    self.dataMux_.unlock();
  }
  const bool bullseyeEnabledPresent =
      jsonFieldPresent(root, "bullseyeMelodyEnabled");
  const bool bullseyeRtttlPresent = jsonFieldPresent(root, "bullseyeRtttl");
  const bool bullseyeConfigSpecified =
      bullseyeEnabledPresent || bullseyeRtttlPresent;
  bool legacyAvoidBbwShotWithoutScale =
      noScaleBbwEnabled(candidate.noScaleBbwMode);
  bool legacyNoScalePresent = false;
  bool legacySerialDebugOutput = false;
  uint8_t requestedSerialLogLevel = 0;
  char customNtp[NTP_SERVER_HOST_CAPACITY] = {};
  memcpy(customNtp, candidate.ntpServerCustom, sizeof(customNtp));
  static const char *const fields[] = {
      "baseRevision", "goalWeightG", "rinseEnabled", "rinseGestureMs", "rinseDurationMs",
      "operationalWallMs", "autoTare", "autoTareOutsideBrew", "postTareBaselineGraceMs",
      "brewByWeight", "canTareStartTimer",
      "scaleTimerStopExtraDelayMs",       "dripDelayMs", "soundAlertsEnabled",
      "firstDropBeep", "scaleConnectedLed",
      "paddleReturnReminderBeep",
      "paddleReturnReminderIntervalMs", "paddleReturnReminderMaxDurationMs",
      "paddleMode", "stopPulseMs", "maxSinglePressMs", "momentaryStartEdge",
      "reedConfirmTimeoutMs", "assumeIdleWhenScaleConnects", "shotReactTimeoutS",
      "buzzerScaleLostBeep", "buzzerAutoToManualGuardEndBeep",
      "buzzerManualNoScaleBeep", "buzzerScaleConnectedBeep",
      "buzzerExtendedPulseRate", "buzzerSlowExtendedPulseRate",
      "bullseyeMelodyEnabled", "bullseyeRtttl",
      "alertOutputChannel", "autoRetare", "retareWindowMs", "minimumCupWeightG",
      "cupRemovedWeightG",
      "retareStabilitySamples", "retareStabilityToleranceG",
      "retareStabilityMaxGapMs", "retareStabilityMinDurationMs",
      "bbwProtectionMs", "fastExtractionGuardEnabled", "avoidAccidentalTouchEnabled",
      "maxRecoveryWeightG",
      "minBbwBrewTimeMs", "slowExtractionGuardEnabled", "minRecoveryWeightG",
      "maxBbwBrewTimeMs", "autoToManualGuardEnabled", "autoToManualGuardLimitMode",
      "autoToManualGuardManualLimitMs", "autoToManualGuardBaselineMs",
      "weightOffsetBaselineG", "timezoneOffsetMinutes", "ntpServerPreset",
      "ntpServerCustom", "scaleMacCacheMode", "bookooMuteOnBuzzerOnly",
      "bookooConnectBeepLevel", "noScaleBbwMode", "avoidBbwShotWithoutScale",
      "lastShotCooldownMs",
      "serialLogLevel", "serialDebugOutput", "ringRetainLogLevel", "showDiagnosticPage",
      "powerManagementEnabled"};
  const char *parseError = nullptr;
  size_t settingFieldCount = 0;
  if (root != nullptr) {
    for (cJSON *item = root->child; item != nullptr; item = item->next) {
      if (item->string != nullptr &&
          strcmp(item->string, "baseRevision") != 0) {
        ++settingFieldCount;
      }
    }
  }
  if (root == nullptr ||
      !jsonHasOnlyUniqueFields(root, fields,
                               sizeof(fields) / sizeof(fields[0]))) {
    parseError = jsonParseFailureMessage(
        "Config must be a JSON object with known fields and correct types.");
  } else if (settingFieldCount == 0) {
    parseError = "Config patch must include at least one setting field.";
  } else if (jsonFieldPresent(root, "baseRevision") &&
             !jsonUint32(root, "baseRevision", baseRevision)) {
    parseError = "baseRevision must be an integer.";
  } else if (jsonFieldPresent(root, "goalWeightG") &&
             !jsonUint8(root, "goalWeightG", candidate.goalWeightG)) {
    parseError = "goalWeightG must be an integer from 10 to 200.";
  } else if (jsonFieldPresent(root, "rinseEnabled") &&
             !jsonBoolean(root, "rinseEnabled", candidate.rinseEnabled)) {
    parseError = "rinseEnabled must be a boolean.";
  } else if (jsonFieldPresent(root, "rinseGestureMs") &&
             !jsonUint32(root, "rinseGestureMs", candidate.rinseGestureMs)) {
    parseError = "rinseGestureMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "rinseDurationMs") &&
             !jsonUint32(root, "rinseDurationMs", candidate.rinseDurationMs)) {
    parseError = "rinseDurationMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "operationalWallMs") &&
             !jsonUint32(root, "operationalWallMs",
                         candidate.operationalWallMs)) {
    parseError = "operationalWallMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "autoTare") &&
             !jsonBoolean(root, "autoTare", candidate.autoTare)) {
    parseError = "autoTare must be a boolean.";
  } else if (jsonFieldPresent(root, "autoTareOutsideBrew") &&
             !jsonBoolean(root, "autoTareOutsideBrew", candidate.autoTareOutsideBrew)) {
    parseError = "autoTareOutsideBrew must be a boolean.";
  } else if (jsonFieldPresent(root, "postTareBaselineGraceMs") &&
             !jsonUint32(root, "postTareBaselineGraceMs",
                         candidate.postTareBaselineGraceMs)) {
    parseError = "postTareBaselineGraceMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "brewByWeight") &&
             !jsonBoolean(root, "brewByWeight", brewByWeight)) {
    parseError = "brewByWeight must be a boolean.";
  } else if (jsonFieldPresent(root, "canTareStartTimer") &&
             !jsonBoolean(root, "canTareStartTimer",
                          candidate.canTareStartTimer)) {
    parseError = "canTareStartTimer must be a boolean.";
  } else if (jsonFieldPresent(root, "scaleTimerStopExtraDelayMs") &&
             !jsonUint32(root, "scaleTimerStopExtraDelayMs",
                         candidate.scaleTimerStopExtraDelayMs)) {
    parseError = "scaleTimerStopExtraDelayMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "dripDelayMs") &&
             !jsonUint32(root, "dripDelayMs", candidate.dripDelayMs)) {
    parseError = "dripDelayMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "soundAlertsEnabled") &&
             !jsonBoolean(root, "soundAlertsEnabled", soundAlertsEnabled)) {
    parseError = "soundAlertsEnabled must be a boolean.";
  } else if (jsonFieldPresent(root, "firstDropBeep") &&
             !jsonBoolean(root, "firstDropBeep", candidate.firstDropBeep)) {
    parseError = "firstDropBeep must be a boolean.";
  } else if (bullseyeEnabledPresent &&
             !jsonBoolean(root, "bullseyeMelodyEnabled",
                          candidateBullseye.enabled)) {
    parseError = "bullseyeMelodyEnabled must be a boolean.";
  } else if (bullseyeRtttlPresent &&
             !jsonString(root, "bullseyeRtttl", candidateBullseye.rtttl,
                         sizeof(candidateBullseye.rtttl), true)) {
    parseError = "bullseyeRtttl must be a string of at most 500 characters.";
  } else if (bullseyeConfigSpecified &&
             !validBullseyeMelodyConfig(candidateBullseye)) {
    parseError = candidateBullseye.enabled && candidateBullseye.rtttl[0] == '\0'
                     ? "Bullseye melody needs an RTTTL tune when enabled."
                     : "bullseyeRtttl must be valid single-line RTTTL (up to 250 notes).";
  } else if (jsonFieldPresent(root, "scaleConnectedLed") &&
             !jsonBoolean(root, "scaleConnectedLed",
                          candidate.scaleConnectedLed)) {
    parseError = "scaleConnectedLed must be a boolean.";
  } else if (jsonFieldPresent(root, "paddleReturnReminderBeep") &&
             !jsonBoolean(root, "paddleReturnReminderBeep",
                          candidate.paddleReturnReminderBeep)) {
    parseError = "paddleReturnReminderBeep must be a boolean.";
  } else if (jsonFieldPresent(root, "paddleReturnReminderIntervalMs") &&
             !jsonUint32(root, "paddleReturnReminderIntervalMs",
                         candidate.paddleReturnReminderIntervalMs)) {
    parseError =
        "paddleReturnReminderIntervalMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "paddleReturnReminderMaxDurationMs") &&
             !jsonUint32(root, "paddleReturnReminderMaxDurationMs",
                         candidate.paddleReturnReminderMaxDurationMs)) {
    parseError =
        "paddleReturnReminderMaxDurationMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "paddleMode") &&
             !jsonPaddleMode(root, "paddleMode", candidate.paddleMode)) {
    parseError = "paddleMode must be auto, natural or original.";
  } else if (jsonFieldPresent(root, "stopPulseMs") &&
             !jsonStopPulseMs(root, candidate)) {
    parseError = "stopPulseMs must be an integer from 50 to 1000.";
  } else if (jsonFieldPresent(root, "maxSinglePressMs") &&
             !jsonMaxSinglePressMs(root, candidate)) {
    parseError = "maxSinglePressMs must be an integer from 100 to 5000.";
  } else if (jsonFieldPresent(root, "momentaryStartEdge") &&
             !jsonMomentaryStartEdge(root, candidate)) {
    parseError = "momentaryStartEdge must be press or release.";
  } else if (jsonFieldPresent(root, "reedConfirmTimeoutMs") &&
             !jsonReedConfirmTimeoutMs(root, candidate)) {
    parseError = "reedConfirmTimeoutMs must be an integer from 200 to 5000.";
  } else if (jsonFieldPresent(root, "assumeIdleWhenScaleConnects") &&
             !jsonAssumeIdleWhenScaleConnects(root, candidate)) {
    parseError = "assumeIdleWhenScaleConnects must be a boolean.";
  } else if (jsonFieldPresent(root, "shotReactTimeoutS") &&
             !jsonShotReactTimeoutS(root, candidate)) {
    parseError =
        "shotReactTimeoutS must be 0 (compiled default) or an integer from 3 to 30.";
  } else if (jsonFieldPresent(root, "buzzerScaleLostBeep") &&
             !jsonBoolean(root, "buzzerScaleLostBeep",
                          candidate.buzzerScaleLostBeep)) {
    parseError = "buzzerScaleLostBeep must be a boolean.";
  } else if (jsonFieldPresent(root, "buzzerAutoToManualGuardEndBeep") &&
             !jsonBoolean(root, "buzzerAutoToManualGuardEndBeep",
                          candidate.buzzerAutoToManualGuardEndBeep)) {
    parseError = "buzzerAutoToManualGuardEndBeep must be a boolean.";
  } else if (jsonFieldPresent(root, "buzzerManualNoScaleBeep") &&
             !jsonBoolean(root, "buzzerManualNoScaleBeep",
                          candidate.buzzerManualNoScaleBeep)) {
    parseError = "buzzerManualNoScaleBeep must be a boolean.";
  } else if (jsonFieldPresent(root, "buzzerScaleConnectedBeep") &&
             !jsonBoolean(root, "buzzerScaleConnectedBeep",
                          candidate.buzzerScaleConnectedBeep)) {
    parseError = "buzzerScaleConnectedBeep must be a boolean.";
  } else if (jsonFieldPresent(root, "buzzerExtendedPulseRate") &&
             !jsonExtendedPulseRate(root, "buzzerExtendedPulseRate",
                                    candidate.buzzerExtendedPulseRate)) {
    parseError =
        "buzzerExtendedPulseRate must be disabled, slow, medium, fast, or rapid.";
  } else if (jsonFieldPresent(root, "buzzerSlowExtendedPulseRate") &&
             !jsonExtendedPulseRate(root, "buzzerSlowExtendedPulseRate",
                                    candidate.buzzerSlowExtendedPulseRate)) {
    parseError =
        "buzzerSlowExtendedPulseRate must be disabled, slow, medium, fast, or "
        "rapid.";
  } else if (jsonFieldPresent(root, "alertOutputChannel") &&
             !jsonAlertOutputChannel(root, "alertOutputChannel",
                                     candidate.alertOutputChannel,
                                     /*optional=*/false)) {
    parseError =
        "alertOutputChannel must be scale_only, buzzer_only, or scale_priority.";
  } else if (jsonFieldPresent(root, "autoRetare") &&
             !jsonBoolean(root, "autoRetare", candidate.autoRetare)) {
    parseError = "autoRetare must be a boolean.";
  } else if (jsonFieldPresent(root, "retareWindowMs") &&
             !jsonUint32(root, "retareWindowMs", candidate.retareWindowMs)) {
    parseError = "retareWindowMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "minimumCupWeightG") &&
             !jsonFloat(root, "minimumCupWeightG",
                        candidate.minimumCupWeightG)) {
    parseError = "minimumCupWeightG must be a number.";
  } else if (jsonFieldPresent(root, "cupRemovedWeightG") &&
             !jsonFloat(root, "cupRemovedWeightG",
                        candidate.cupRemovedWeightG)) {
    parseError = "cupRemovedWeightG must be a number.";
  } else if (jsonFieldPresent(root, "retareStabilitySamples") &&
             !jsonUint8(root, "retareStabilitySamples",
                        candidate.retareStabilitySamples)) {
    parseError = "retareStabilitySamples must be an integer from 2 to 10.";
  } else if (jsonFieldPresent(root, "retareStabilityToleranceG") &&
             !jsonFloat(root, "retareStabilityToleranceG",
                        candidate.retareStabilityToleranceG)) {
    parseError = "retareStabilityToleranceG must be a number.";
  } else if (jsonFieldPresent(root, "retareStabilityMaxGapMs") &&
             !jsonUint32(root, "retareStabilityMaxGapMs",
                         candidate.retareStabilityMaxGapMs)) {
    parseError = "retareStabilityMaxGapMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "retareStabilityMinDurationMs") &&
             !jsonUint32(root, "retareStabilityMinDurationMs",
                         candidate.retareStabilityMinDurationMs)) {
    parseError =
        "retareStabilityMinDurationMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "bbwProtectionMs") &&
             !jsonUint32(root, "bbwProtectionMs", candidate.bbwProtectionMs)) {
    parseError = "bbwProtectionMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "fastExtractionGuardEnabled") &&
             !jsonBoolean(root, "fastExtractionGuardEnabled",
                          candidate.fastExtractionGuardEnabled)) {
    parseError = "fastExtractionGuardEnabled must be a boolean.";
  } else if (jsonFieldPresent(root, "avoidAccidentalTouchEnabled") &&
             !jsonBoolean(root, "avoidAccidentalTouchEnabled",
                          candidate.avoidAccidentalTouchEnabled)) {
    parseError = "avoidAccidentalTouchEnabled must be a boolean.";
  } else if (jsonFieldPresent(root, "maxRecoveryWeightG") &&
             !jsonFloat(root, "maxRecoveryWeightG",
                        candidate.maxRecoveryWeightG)) {
    parseError = "maxRecoveryWeightG must be a number.";
  } else if (jsonFieldPresent(root, "minBbwBrewTimeMs") &&
             !jsonUint32(root, "minBbwBrewTimeMs", candidate.minBbwBrewTimeMs)) {
    parseError = "minBbwBrewTimeMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "slowExtractionGuardEnabled") &&
             !jsonBoolean(root, "slowExtractionGuardEnabled",
                          candidate.slowExtractionGuardEnabled)) {
    parseError = "slowExtractionGuardEnabled must be a boolean.";
  } else if (jsonFieldPresent(root, "minRecoveryWeightG") &&
             !jsonFloat(root, "minRecoveryWeightG",
                        candidate.minRecoveryWeightG)) {
    parseError = "minRecoveryWeightG must be a number.";
  } else if (jsonFieldPresent(root, "maxBbwBrewTimeMs") &&
             !jsonUint32(root, "maxBbwBrewTimeMs", candidate.maxBbwBrewTimeMs)) {
    parseError = "maxBbwBrewTimeMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "autoToManualGuardEnabled") &&
             !jsonBoolean(root, "autoToManualGuardEnabled",
                          candidate.autoToManualGuardEnabled)) {
    parseError = "autoToManualGuardEnabled must be a boolean.";
  } else if (jsonFieldPresent(root, "autoToManualGuardLimitMode") &&
             !jsonAutoToManualGuardLimitMode(
                 root, "autoToManualGuardLimitMode",
                 candidate.autoToManualGuardLimitMode)) {
    parseError = "autoToManualGuardLimitMode must be \"manual\" or \"auto\".";
  } else if (jsonFieldPresent(root, "autoToManualGuardManualLimitMs") &&
             !jsonUint32(root, "autoToManualGuardManualLimitMs",
                         candidate.autoToManualGuardManualLimitMs)) {
    parseError =
        "autoToManualGuardManualLimitMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "autoToManualGuardBaselineMs") &&
             !jsonUint32(root, "autoToManualGuardBaselineMs",
                         candidate.autoToManualGuardBaselineMs)) {
    parseError =
        "autoToManualGuardBaselineMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "weightOffsetBaselineG") &&
             !jsonFloat(root, "weightOffsetBaselineG",
                        candidate.weightOffsetBaselineG)) {
    parseError = "weightOffsetBaselineG must be a number.";
  } else if (jsonFieldPresent(root, "timezoneOffsetMinutes") &&
             !jsonInt16(root, "timezoneOffsetMinutes",
                        candidate.timezoneOffsetMinutes)) {
    parseError = "timezoneOffsetMinutes must be an integer.";
  } else if (jsonFieldPresent(root, "ntpServerPreset") &&
             !jsonNtpPreset(root, "ntpServerPreset",
                            candidate.ntpServerPreset)) {
    parseError = "ntpServerPreset must be pool, google, cloudflare, or nist.";
  } else if (jsonFieldPresent(root, "ntpServerCustom") &&
             !jsonString(root, "ntpServerCustom", customNtp, sizeof(customNtp),
                         true)) {
    parseError = "ntpServerCustom must be a string of at most 63 characters.";
  } else if (jsonFieldPresent(root, "scaleMacCacheMode") &&
             !jsonScaleMacCacheMode(root, "scaleMacCacheMode",
                                    candidate.scaleMacCacheMode)) {
    parseError = "scaleMacCacheMode must be first, prefer, or only.";
  } else if (jsonFieldPresent(root, "bookooMuteOnBuzzerOnly") &&
             !jsonBoolean(root, "bookooMuteOnBuzzerOnly",
                          candidate.bookooMuteOnBuzzerOnly)) {
    parseError = "bookooMuteOnBuzzerOnly must be a boolean.";
  } else if (jsonFieldPresent(root, "bookooConnectBeepLevel") &&
             (!jsonUint8(root, "bookooConnectBeepLevel",
                         candidate.bookooConnectBeepLevel) ||
              candidate.bookooConnectBeepLevel > BOOKOO_BEEP_LEVEL_MAX)) {
    parseError = "bookooConnectBeepLevel must be an integer from 0 to 5.";
  } else if (jsonFieldPresent(root, "noScaleBbwMode") &&
             jsonFieldPresent(root, "avoidBbwShotWithoutScale")) {
    parseError =
        "Send noScaleBbwMode or avoidBbwShotWithoutScale, not both.";
  } else if (jsonFieldPresent(root, "noScaleBbwMode") &&
             !jsonNoScaleBbwMode(root, "noScaleBbwMode",
                                 candidate.noScaleBbwMode)) {
    parseError =
        "noScaleBbwMode must be off, warn_once, or require_scale.";
  } else if (jsonFieldPresent(root, "avoidBbwShotWithoutScale") &&
             !jsonBoolean(root, "avoidBbwShotWithoutScale",
                          legacyAvoidBbwShotWithoutScale)) {
    parseError = "avoidBbwShotWithoutScale must be a boolean.";
  } else if (jsonFieldPresent(root, "lastShotCooldownMs") &&
             !jsonUint32(root, "lastShotCooldownMs",
                         candidate.lastShotCooldownMs)) {
    parseError = "lastShotCooldownMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "serialLogLevel") &&
             !jsonLogLevel(root, "serialLogLevel", requestedSerialLogLevel)) {
    parseError =
        "serialLogLevel must be none, critical, error, warning, info, or debug.";
  } else if (jsonFieldPresent(root, "serialDebugOutput") &&
             !jsonBoolean(root, "serialDebugOutput",
                          legacySerialDebugOutput)) {
    parseError = "serialDebugOutput must be a boolean.";
  } else if (jsonFieldPresent(root, "serialLogLevel")) {
    setSerialLogLevel(candidate,
                      static_cast<LogLevel>(requestedSerialLogLevel));
  } else if (jsonFieldPresent(root, "serialDebugOutput")) {
    setSerialLogLevel(candidate, legacySerialDebugOutput ? LogLevel::INFO
                                                          : LogLevel::NONE);
  } else if (jsonFieldPresent(root, "showDiagnosticPage") &&
             !jsonBoolean(root, "showDiagnosticPage",
                          candidate.showDiagnosticPage)) {
    parseError = "showDiagnosticPage must be a boolean.";
  } else if (jsonFieldPresent(root, "ringRetainLogLevel") &&
             !jsonLogLevel(root, "ringRetainLogLevel",
                           candidate.ringRetainLogLevel)) {
    parseError =
        "ringRetainLogLevel must be none, critical, error, warning, info, or "
        "debug.";
  }
  if (parseError == nullptr && jsonFieldPresent(root, "powerManagementEnabled") &&
      !jsonBoolean(root, "powerManagementEnabled", candidate.powerManagementEnabled)) {
    parseError = "powerManagementEnabled must be a boolean.";
  }
  if (root != nullptr) {
    brewByWeightPresent = jsonFieldPresent(root, "brewByWeight");
    baseRevisionPresent = jsonFieldPresent(root, "baseRevision");
    legacyNoScalePresent =
        jsonFieldPresent(root, "avoidBbwShotWithoutScale");
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (parseError != nullptr) {
    memset(customNtp, 0, sizeof(customNtp));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     parseError);
  }
  if (dateTimePatch && !self.requireAdminUnlock(request)) {
    memset(customNtp, 0, sizeof(customNtp));
    return ESP_OK;
  }
  if (diagnosticLogPatch && !self.diagnosticPageEnabled() &&
      !self.requireAdminUnlock(request)) {
    memset(customNtp, 0, sizeof(customNtp));
    return ESP_OK;
  }
  if (diagnosticPagePatch && !self.requireAdminUnlock(request)) {
    memset(customNtp, 0, sizeof(customNtp));
    return ESP_OK;
  }
  if (baseRevisionPresent && baseRevision != candidate.revision) {
    memset(customNtp, 0, sizeof(customNtp));
    return sendError(request, STATUS_CONFLICT, "CONFIG_REVISION_STALE",
                     "Config changed; refresh and retry.");
  }
  if (brewByWeightPresent) {
    candidate.timerOnly = !brewByWeight;
  }
  if (legacyNoScalePresent) {
    candidate.noScaleBbwMode = static_cast<uint8_t>(
        legacyAvoidBbwShotWithoutScale ? NoScaleBbwMode::WARN_ONCE
                                       : NoScaleBbwMode::OFF);
  }
  candidate.soundAlertsMuted = !soundAlertsEnabled;
  memcpy(candidate.ntpServerCustom, customNtp, sizeof(candidate.ntpServerCustom));
  memset(customNtp, 0, sizeof(customNtp));
  // ControlGateSnapshot is gate-only (no recipe bank). Validate the patched
  // machine config against the live preset overlay, same as APPLY_CONFIG.
  ShotPresetBank livePresets = {};
  if (self.callbacks_.copyPresetBank != nullptr) {
    self.callbacks_.copyPresetBank(&livePresets);
  } else {
    self.dataMux_.lock();
    livePresets = self.settings_.presets;
    self.dataMux_.unlock();
  }
  const RuntimeConfig effective =
      composeEffectiveConfig(candidate, livePresets);
  const ConfigValidationError error = validateRuntimeConfig(effective);
  if (error != ConfigValidationError::NONE) {
    self.log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED,
             static_cast<int32_t>(error));
    char errorBody[320] = {};
    snprintf(errorBody, sizeof(errorBody),
             "{\"error\":\"INVALID_CONFIG\",\"message\":\"%s\",\"field\":\"%s\"}",
             configValidationMessage(error), configValidationErrorName(error));
    return sendJson(request, STATUS_UNPROCESSABLE, errorBody);
  }
  WebCommand command;
  command.type = WebCommandType::APPLY_CONFIG;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  command.config = candidate;
  command.bullseyeConfigSpecified = bullseyeConfigSpecified;
  if (bullseyeConfigSpecified) {
    if (self.callbacks_.stageBullseyeConfig == nullptr ||
        !self.callbacks_.stageBullseyeConfig(candidateBullseye,
                                              command.requestId)) {
      return sendError(request, STATUS_UNAVAILABLE, "CONTROL_BUSY",
                       "Control is busy; the Bullseye melody was not staged.");
    }
    command.bullseyeStageRequestId = command.requestId;
  }
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; nothing was saved.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::webhookHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  const ControlGateSnapshot gate = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, gate)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Webhooks can be changed or tested only while Ready.");
  }
  if (!self.requireAdminUnlock(request)) return ESP_OK;
  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "A bounded webhook JSON request is required.");
  if (bodyStatus != ESP_OK) return bodyStatus;

  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  static const char *const fields[] = {
      "action", "enabled", "url", "brewState", "firstDrop", "end",
      "deferDuringShot"};
  char action[8] = {};
  WebhookConfig candidate = self.webhooks_.config();
  const bool known = root != nullptr &&
      jsonHasOnlyUniqueFields(root, fields, sizeof(fields) / sizeof(fields[0]));
  const bool actionOk = known &&
      jsonString(root, "action", action, sizeof(action), false);
  bool parsed = actionOk;
  if (parsed && strcmp(action, "save") == 0) {
    parsed = jsonBoolean(root, "enabled", candidate.enabled) &&
             jsonString(root, "url", candidate.url, sizeof(candidate.url), true) &&
             jsonBoolean(root, "brewState", candidate.brewState) &&
             jsonBoolean(root, "firstDrop", candidate.firstDrop) &&
             jsonBoolean(root, "end", candidate.end) &&
             jsonBoolean(root, "deferDuringShot", candidate.deferDuringShot) &&
             validWebhookConfig(candidate);
  } else if (parsed && strcmp(action, "test") == 0) {
    parsed = validWebhookUrl(candidate.url);
  } else {
    parsed = false;
  }
  if (root != nullptr) cJSON_Delete(root);
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_WEBHOOK",
                     "Use an http:// URL without embedded credentials; HTTPS is not supported.");
  }

  if (strcmp(action, "test") == 0) {
    WebhookEvent event;
    event.type = WebhookEventType::TEST;
    event.uptimeMs = millis();
    event.unixSec = g_wallClock.synced() ? g_wallClock.nowUtcSec(millis()) : 0;
    if (!self.webhooks_.enqueue(event)) {
      return sendError(request, STATUS_UNAVAILABLE, "WEBHOOK_UNAVAILABLE",
                       "The webhook worker is unavailable or its queue is full.");
    }
    return sendJson(request, STATUS_ACCEPTED, "{\"queued\":true}");
  }

  WebCommand command;
  command.type = WebCommandType::SAVE_WEBHOOK;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  command.webhookStageRequestId = command.requestId;
  self.dataMux_.lock();
  const bool stageAvailable = self.stagedWebhookRequestId_ == 0;
  if (stageAvailable) {
    self.stagedWebhook_ = candidate;
    self.stagedWebhookRequestId_ = command.requestId;
  }
  self.dataMux_.unlock();
  if (!stageAvailable) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_BUSY",
                     "Another webhook configuration save is still pending.");
  }
  if (!self.callbacks_.enqueueWebCommand(command)) {
    self.clearStagedWebhook(command.requestId);
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; webhook settings were not saved.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::bullseyeTestHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  const ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "The Bullseye melody can be tested only while Ready.");
  }
  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "A Bullseye RTTTL test request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  // This handler owns NetworkWorkBuf until unlockJsonBody(). Keep the 502-byte
  // candidate in its PSRAM scratch instead of consuming HTTP task stack.
  BullseyeMelodyConfig &testConfig = self.workBuf_->bullseyeMelody;
  testConfig = BullseyeMelodyConfig{};
  static const char *const fields[] = {"bullseyeRtttl"};
  const bool parsed = root != nullptr &&
                      jsonHasOnlyUniqueFields(root, fields, 1) &&
                      jsonString(root, "bullseyeRtttl", testConfig.rtttl,
                                 sizeof(testConfig.rtttl), true) &&
                      validBullseyeRtttl(testConfig.rtttl);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  if (!parsed) {
    self.unlockJsonBody();
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_BULLSEYE_RTTTL",
                     "bullseyeRtttl must be valid single-line RTTTL (up to 250 notes).");
  }

  WebCommand command;
  command.type = WebCommandType::BUZZER_TEST;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  command.bullseyeConfigSpecified = true;
  command.bullseyeStageRequestId = command.requestId;
  const bool staged = self.callbacks_.stageBullseyeConfig != nullptr &&
                      self.callbacks_.stageBullseyeConfig(testConfig,
                                                          command.requestId);
  self.unlockJsonBody();
  if (!staged) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_BUSY",
                     "Control is busy; the Bullseye melody was not staged.");
  }
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; the Bullseye melody was not played.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::preferredScaleClearHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "The paired scale cannot be forgotten while a cycle "
                     "is active.");
  }
  WebCommand command;
  command.type = WebCommandType::CLEAR_PREFERRED_SCALE;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; nothing was cleared.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::preferredScaleSelectHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "The preferred scale cannot be changed while a cycle "
                     "is active.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "A bounded JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  char mac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  char name[PREFERRED_SCALE_NAME_CAPACITY] = {};
  const char *parseError = nullptr;
  if (root == nullptr) {
    parseError = jsonParseFailureMessage("JSON body is required.");
  } else if (!jsonString(root, "mac", mac, sizeof(mac), true)) {
    parseError = "mac must be a string (empty clears the preferred scale).";
  } else if (mac[0] != '\0' && !validPreferredScaleMac(mac)) {
    parseError = "mac must be empty or a BLE address AA:BB:CC:DD:EE:FF.";
  } else {
    cJSON *nameItem = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (nameItem != nullptr) {
      if (!jsonString(root, "name", name, sizeof(name), true) ||
          !validPreferredScaleName(name)) {
        parseError = "name contains unsupported characters.";
      }
    }
  }
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (parseError != nullptr) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST", parseError);
  }

  WebCommand command;
  command.setNetworkType(WebCommandType::SELECT_PREFERRED_SCALE);
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  copyCString(command.network.scaleSelectMac, sizeof(command.network.scaleSelectMac), mac);
  copyCString(command.network.scaleSelectName, sizeof(command.network.scaleSelectName), name);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; nothing was selected.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::presetsHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Presets are locked while a cycle is active.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "A bounded JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  char action[24] = {};
  uint8_t presetId = 0;
  char presetName[SHOT_PRESET_NAME_CAPACITY] = {};
  const char *parseError = nullptr;
  PresetAction presetAction = PresetAction::APPLY;
  WebCommand command;
  command.type = WebCommandType::PRESET_OP;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (self.callbacks_.copyRuntimeConfig != nullptr) {
    self.callbacks_.copyRuntimeConfig(&command.config);
  }

  if (root == nullptr || !jsonString(root, "action", action, sizeof(action),
                                     false)) {
    parseError = "action is required.";
  } else if (strcmp(action, "apply") == 0 || strcmp(action, "load") == 0) {
    presetAction = PresetAction::APPLY;
    if (!jsonUint8(root, "id", presetId) || presetId == 0) {
      parseError = "id must be a non-zero integer.";
    }
  } else if (strcmp(action, "create") == 0 || strcmp(action, "new") == 0) {
    presetAction = PresetAction::CREATE;
  } else if (strcmp(action, "delete") == 0) {
    presetAction = PresetAction::DELETE;
    if (!jsonUint8(root, "id", presetId) || presetId == 0) {
      parseError = "id must be a non-zero integer.";
    }
  } else if (strcmp(action, "duplicate") == 0) {
    presetAction = PresetAction::DUPLICATE;
    if (!jsonUint8(root, "id", presetId) || presetId == 0) {
      parseError = "id must be a non-zero integer.";
    }
  } else if (strcmp(action, "rename") == 0) {
    presetAction = PresetAction::RENAME;
    if (!jsonUint8(root, "id", presetId) || presetId == 0) {
      parseError = "id must be a non-zero integer.";
    } else if (!jsonString(root, "name", presetName, sizeof(presetName),
                           false)) {
      parseError = "name is required.";
    }
  } else if (strcmp(action, "restore_factory_values") == 0) {
    presetAction = PresetAction::RESTORE_FACTORY_VALUES;
    if (!jsonUint8(root, "id", presetId) || presetId == 0) {
      parseError = "id must be a non-zero integer.";
    }
  } else if (strcmp(action, "save") == 0 || strcmp(action, "update") == 0) {
    presetAction = PresetAction::SAVE;
    if (!jsonUint8(root, "id", presetId) || presetId == 0) {
      parseError = "id must be a non-zero integer.";
    } else {
      bool brewByWeight = true;
      if (!jsonBoolean(root, "brewByWeight", brewByWeight) ||
          !jsonUint8(root, "goalWeightG", command.config.goalWeightG) ||
          !jsonUint32(root, "operationalWallMs",
                      command.config.operationalWallMs) ||
          !jsonUint32(root, "bbwProtectionMs",
                      command.config.bbwProtectionMs) ||
          (jsonFieldPresent(root, "weightOffsetBaselineG") &&
           !jsonFloat(root, "weightOffsetBaselineG",
                      command.config.weightOffsetBaselineG)) ||
          !jsonBoolean(root, "fastExtractionGuardEnabled",
                       command.config.fastExtractionGuardEnabled) ||
          !jsonFloat(root, "maxRecoveryWeightG",
                     command.config.maxRecoveryWeightG) ||
          !jsonUint32(root, "minBbwBrewTimeMs", command.config.minBbwBrewTimeMs) ||
          !jsonBoolean(root, "slowExtractionGuardEnabled",
                       command.config.slowExtractionGuardEnabled) ||
          !jsonFloat(root, "minRecoveryWeightG",
                     command.config.minRecoveryWeightG) ||
          !jsonUint32(root, "maxBbwBrewTimeMs", command.config.maxBbwBrewTimeMs) ||
          !jsonBoolean(root, "autoToManualGuardEnabled",
                       command.config.autoToManualGuardEnabled) ||
          !jsonAutoToManualGuardLimitMode(
              root, "autoToManualGuardLimitMode",
              command.config.autoToManualGuardLimitMode) ||
          !jsonUint32(root, "autoToManualGuardManualLimitMs",
                      command.config.autoToManualGuardManualLimitMs) ||
          !jsonUint32(root, "autoToManualGuardBaselineMs",
                      command.config.autoToManualGuardBaselineMs) ||
          !jsonBoolean(root, "cupProtectionEnabled",
                       command.config.cupProtectionEnabled) ||
          !jsonBoolean(root, "stopIfCupRemoved",
                       command.config.stopIfCupRemoved) ||
          !jsonBoolean(root, "requireCupToStart",
                       command.config.requireCupToStart) ||
          !jsonBoolean(root, "avoidAccidentalTouchEnabled",
                       command.config.avoidAccidentalTouchEnabled)) {
        parseError = "save requires the full Brew recipe field set.";
      } else {
        command.config.timerOnly = !brewByWeight;
        command.bbwAlgorithmSpecified = jsonFieldPresent(root, "bbwAlgorithm");
        command.bbwBaselineSpecified = jsonFieldPresent(root, "weightOffsetBaselineG");
        const cJSON *alphaBase = cJSON_GetObjectItemCaseSensitive(root, "bbwAlphaBaseline");
        if (alphaBase && (!cJSON_IsNumber(alphaBase) ||
            !parseBbwAlphaBaseline(alphaBase->valuedouble, command.bbwAlphaBaseline)))
          parseError = "bbwAlphaBaseline must be 0.01–1.00 in steps of 0.01.";
        char algorithm[20] = {};
        if (command.bbwAlgorithmSpecified &&
            (!jsonString(root, "bbwAlgorithm", algorithm, sizeof(algorithm), false) ||
             !parseBbwAlgorithm(algorithm, command.config.bbwAlgorithm)))
          parseError = "bbwAlgorithm must be legacy or linear_ewma.";
        if (jsonFieldPresent(root, "bbwAlpha") ||
            jsonFieldPresent(root, "bbwEwmaAlpha") ||
            jsonFieldPresent(root, "bbwAlphaLearned") ||
            jsonFieldPresent(root, "bbwEwmaOffsetG"))
          parseError = "BBW learning state is read-only.";
      }
    }
  } else {
    parseError = "Unknown preset action.";
  }

  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (parseError != nullptr) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD", parseError);
  }

  command.presetAction = static_cast<uint8_t>(presetAction);
  command.presetId = presetId;
  copyCString(command.presetName, sizeof(command.presetName), presetName);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; nothing was saved.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::resetCalibrationHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Calibration is locked while a cycle is active.");
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "An empty JSON object is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  static const char *const resetFields[] = {"fullEwma", "baseRevision"};
  bool fullEwma = false;
  uint32_t baseRevision = 0;
  const bool revisionSpecified = root && jsonFieldPresent(root, "baseRevision");
  const bool parsed = root != nullptr &&
      jsonHasOnlyUniqueFields(root, resetFields, 2) &&
      (!revisionSpecified || jsonUint32(root, "baseRevision", baseRevision)) &&
      (!jsonFieldPresent(root, "fullEwma") || jsonBoolean(root, "fullEwma", fullEwma));
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_REQUEST",
                     "Use an empty object or a boolean fullEwma field.");
  }
  WebCommand command;
  command.type = WebCommandType::RESET_WEIGHT_OFFSET;
  command.bbwFullReset = fullEwma;
  command.bbwResetRevisionSpecified = revisionSpecified;
  command.config.revision = baseRevision;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; calibration was not reset.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::resetGuardSamplesHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Guard samples are locked while a cycle is active.");
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "An empty JSON object is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  static const char *const noFields[] = {nullptr};
  const bool parsed = root != nullptr &&
                      jsonHasOnlyUniqueFields(root, noFields, 0);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_REQUEST",
                     "The guard samples reset request must be an empty object.");
  }
  WebCommand command;
  command.type = WebCommandType::RESET_AUTO_TO_MANUAL_GUARD_SAMPLES;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; guard samples were not reset.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::paddleHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON body is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  bool on = false;
  static const char *const fields[] = {"on"};
  const bool parsed = root != nullptr &&
                      jsonHasOnlyUniqueFields(root, fields, 1) &&
                      jsonBoolean(root, "on", on);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     "The on field must be boolean.");
  }
  ControlGateSnapshot status = self.controlGate();
  if (on && !REMOTE_MACHINE_CONTROL_ENABLED) {
    return sendError(request, "403 Forbidden", "REMOTE_CONTROL_DISABLED",
                     "Remote machine control actuation is disabled in this firmware build.");
  }
  const bool allowed = on ? self.webUiConfigurationAllowed(request, status)
                          : (status.activeCycle &&
                             status.source == ControlSource::WEB);
  if (!allowed) {
    return sendError(request, STATUS_CONFLICT, "CONTROL_STATE_CONFLICT",
                     "The current state does not allow that action.");
  }
  WebCommand command;
  command.type = on ? WebCommandType::REMOTE_ON : WebCommandType::REMOTE_OFF;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::rinseHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  ControlGateSnapshot status = self.controlGate();
  if (!REMOTE_MACHINE_CONTROL_ENABLED) {
    return sendError(request, "403 Forbidden", "REMOTE_CONTROL_DISABLED",
                     "Remote machine control actuation is disabled in this firmware build.");
  }
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT, "CONTROL_STATE_CONFLICT",
                     "Rinse can only start from Ready.");
  }
  WebCommand command;
  command.type = WebCommandType::RINSE;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::stopHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  ControlGateSnapshot status = self.controlGate();
  if (!status.activeCycle || !status.machineRunning) {
    return sendError(request, STATUS_CONFLICT, "CONTROL_STATE_CONFLICT",
                     "There is no active shot to stop.");
  }
  WebCommand command;
  command.type = WebCommandType::STOP;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::forcePulseHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  if (!REMOTE_MACHINE_CONTROL_ENABLED) {
    return sendError(request, "403 Forbidden", "REMOTE_CONTROL_DISABLED",
                     "Remote machine control actuation is disabled in this firmware build.");
  }
  if (SHOT_STOPPER_MACHINE_TYPE == 0) {
    return sendError(request, STATUS_CONFLICT, "MACHINE_TYPE_UNSUPPORTED",
                     "Forced switch pulses require a momentary machine build.");
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "An empty JSON object is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  static const char *const noFields[] = {nullptr};
  const bool parsed = root != nullptr &&
                      jsonHasOnlyUniqueFields(root, noFields, 0);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_REQUEST",
                     "The forced pulse request must be an empty object.");
  }
  WebCommand command;
  command.type = WebCommandType::FORCE_SWITCH_PULSE;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::stateOverrideHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (SHOT_STOPPER_MACHINE_TYPE != 1) {
    return sendError(request, STATUS_CONFLICT, "MACHINE_TYPE_UNSUPPORTED",
                     "Inferred-state override is only available on switch-only builds.");
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON body is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  char state[8] = {};
  static const char *const fields[] = {"state"};
  const bool parsed = root != nullptr &&
                      jsonHasOnlyUniqueFields(root, fields, 1) &&
                      jsonString(root, "state", state, sizeof(state), false);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed || (strcmp(state, "off") != 0 && strcmp(state, "on") != 0)) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     "The state field must be \"off\" or \"on\".");
  }
  WebCommand command;
  command.type = strcmp(state, "off") == 0 ? WebCommandType::STATE_OVERRIDE_OFF
                                           : WebCommandType::STATE_OVERRIDE_ON;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::restartHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  // Shot always wins. Queue the restart even mid-cycle; the control task
  // holds it until idle instead of opening K1 to make way for the reset.
  // Override must not cut a pour.
  WebCommand command;
  command.type = WebCommandType::RESTART;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::clearResetHistoryHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  // Public while the diagnostic page is enabled; Admin unlock otherwise.
  if (!self.diagnosticPageEnabled() && !self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "An explicit reset-history confirmation is required.");
  if (bodyStatus != ESP_OK) return bodyStatus;
  char confirmation[32] = {};
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  static const char *const fields[] = {"confirm"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "confirm", confirmation, sizeof(confirmation), false) &&
      strcmp(confirmation, "CLEAR_RESET_HISTORY") == 0;
  if (root != nullptr) cJSON_Delete(root);
  self.unlockJsonBody();
  memset(confirmation, 0, sizeof(confirmation));
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_CONFIRMATION",
                     "Confirm reset-history clearing explicitly.");
  }
  WebCommand command;
  command.type = WebCommandType::CLEAR_RESET_HISTORY;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; reset history was not cleared.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::factoryResetHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical activator OFF, and wait for Ready before restoring factory settings.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "An explicit factory-reset confirmation is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  char confirmation[32] = {};
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  static const char *const fields[] = {"confirm"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "confirm", confirmation, sizeof(confirmation), false) &&
      strcmp(confirmation, "ERASE_ALL_SETTINGS") == 0;
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  memset(confirmation, 0, sizeof(confirmation));
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE,
                     "FACTORY_RESET_NOT_CONFIRMED",
                     "The factory reset was not explicitly confirmed.");
  }

  WebCommand command;
  command.type = WebCommandType::FACTORY_RESET;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; no settings were erased.");
  }
  return self.sendAccepted(request, command.requestId,
                           "\"state\":\"QUEUED\"");
}
