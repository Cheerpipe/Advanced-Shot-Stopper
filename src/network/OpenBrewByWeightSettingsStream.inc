// View-scoped Settings projection: a thin overlay over Home's core state
// stream (D17 — every field Home already carries was removed here; the
// client composes this frame over Home's standing model). The bounded
// sender contract of sendHomeStream/sendDiagnosticStream still holds: every
// scalar is formatted and fingerprinted per dispatch and reaches the wire
// only when its hash changes, so one changed setting travels alone and an
// unchanged projection costs no frame. Live fields that move without a
// revision bump (bbw learning readback, the scale-loss projection, the
// Linea Micra section) ride the same per-field hashing; the scale-history
// and Bullseye RTTTL blobs sit behind render caches. Every value comes
// from the control snapshot and the PSRAM cache callbacks; the sender
// never reads flash (D10). sendHomeStream filled g_work->scaleHistory and
// g_work->presetBank earlier in this same dispatch.
__attribute__((optimize("Os")))
bool OpenBrewByWeightNetwork::sendSettingsStream(UiStreamSession &session,
                                           const ControlStatusSnapshot &control,
                                           const NetworkStatusSnapshot &network) {
  uiStreamSendKind = "settings";
  // The overlay no longer reads the network snapshot (lastCommand left with
  // the Home-duplicated fields); the uniform sender-chain signature stays.
  (void)network;
  const bool initial =
      session.settingsResync || session.settingsBoot != control.bootId;
  auto &hashes = workBuf_->uiFingerprints[&session - uiStreams_];
  HomeStreamDelta delta{hashes.settingsHashes,
                        UiStreamFingerprints::kSettingsFields, 0, 0, 0, initial};
  // A snapshot names its identity and subscription; an increment continues.
  delta.ok = initial
      ? statusJsonAppend(&delta.used,
            "{\"v\":1,\"type\":\"settings\",\"boot\":%lu,\"snapshot\":true,"
            "\"request\":%lu,\"changes\":{",
            static_cast<unsigned long>(control.bootId),
            static_cast<unsigned long>(session.settingsRequest))
      : statusJsonAppend(&delta.used,
            "{\"type\":\"settings\",\"request\":%lu,\"changes\":{",
            static_cast<unsigned long>(session.settingsRequest));
  delta.field("buzzerSupported", bool(BUZZER_SUPPORT_ENABLED));
  const auto &config = control.config;
  const auto &activePreset = activeShotPreset(g_work->presetBank);
  // Finite or "null" like the float overload (D15): a non-finite value must
  // never render as bare nan and poison the frame.
  delta.field("config.weightOffsetG",
              std::isfinite(config.weightOffsetG) ? "%.2f" : "null",
              static_cast<double>(config.weightOffsetG));
  delta.field("config.weightOffsetBaselineG",
              std::isfinite(config.weightOffsetBaselineG) ? "%.2f" : "null",
              static_cast<double>(config.weightOffsetBaselineG));
  delta.field("config.autoTare", bool(config.autoTare));
  delta.field("config.autoTareOutsideBrew", bool(config.autoTareOutsideBrew));
  delta.field("config.retareAccessoryOutsideBrew",
              bool(idleAccessoryRetareEnabled(config.noScaleBbwMode)));
  delta.field("config.postTareBaselineGraceMs",
              static_cast<unsigned long>(config.postTareBaselineGraceMs));
  delta.field("config.canTareStartTimer", bool(config.canTareStartTimer));
  delta.field("config.scaleTimerStopExtraDelayMs",
              static_cast<unsigned long>(config.scaleTimerStopExtraDelayMs));
  delta.field("config.dripDelayMs", static_cast<unsigned long>(config.dripDelayMs));
  delta.field("config.firstDropBeep", bool(config.firstDropBeep));
  delta.field("config.paddleReturnReminderBeep",
              bool(config.paddleReturnReminderBeep));
  delta.field("config.paddleReturnReminderIntervalMs",
              static_cast<unsigned long>(config.paddleReturnReminderIntervalMs));
  delta.field("config.paddleReturnReminderMaxDurationMs",
              static_cast<unsigned long>(
                  config.paddleReturnReminderMaxDurationMs));
  delta.field("config.paddleMode", "\"%s\"", paddleModeId(config.paddleMode));
  delta.field("config.stopPulseMs",
              static_cast<unsigned long>(runtimeStopPulseMs(config)));
  delta.field("config.maxSinglePressMs",
              static_cast<unsigned long>(runtimeMaxSinglePressMs(config)));
  delta.field("config.momentaryStartEdge", "\"%s\"",
              momentaryStartEdgeId(config.momentaryStartOnPress));
  delta.field("config.reedConfirmTimeoutMs",
              static_cast<unsigned long>(runtimeReedConfirmTimeoutMs(config)));
  delta.field("config.assumeIdleWhenScaleConnects",
              bool(config.assumeIdleWhenScaleConnects));
  delta.field("config.shotReactTimeoutS", "%u",
              static_cast<unsigned>(runtimeShotReactTimeoutS(config)));
  delta.field("config.buzzerScaleLostBeep", bool(config.buzzerScaleLostBeep));
  delta.field("config.buzzerAutoToManualGuardEndBeep",
              bool(config.buzzerAutoToManualGuardEndBeep));
  delta.field("config.buzzerManualNoScaleBeep",
              bool(config.buzzerManualNoScaleBeep));
  delta.field("config.buzzerScaleConnectedBeep",
              bool(config.buzzerScaleConnectedBeep));
  delta.field("config.scaleConnectedLed", bool(config.scaleConnectedLed));
  delta.field("config.buzzerExtendedPulseRate", "\"%s\"",
              extendedPulseRateId(config.buzzerExtendedPulseRate));
  delta.field("config.buzzerSlowExtendedPulseRate", "\"%s\"",
              extendedPulseRateId(config.buzzerSlowExtendedPulseRate));
  delta.field("config.alertOutputChannel", "\"%s\"",
              alertOutputChannelId(config.alertOutputChannel));
  delta.field("config.rinseGestureMs",
              static_cast<unsigned long>(config.rinseGestureMs));
  delta.field("config.rinseDurationMs",
              static_cast<unsigned long>(config.rinseDurationMs));
  delta.field("config.autoRetare", bool(config.autoRetare));
  delta.field("config.retareWindowMs",
              static_cast<unsigned long>(config.retareWindowMs));
  delta.field("config.minimumCupWeightG", config.minimumCupWeightG);
  delta.field("config.retareStabilitySamples", "%u",
              static_cast<unsigned>(config.retareStabilitySamples));
  delta.field("config.retareStabilityToleranceG",
              config.retareStabilityToleranceG);
  delta.field("config.retareStabilityMaxGapMs",
              static_cast<unsigned long>(config.retareStabilityMaxGapMs));
  delta.field("config.retareStabilityMinDurationMs",
              static_cast<unsigned long>(config.retareStabilityMinDurationMs));
  delta.field("config.bbwProtectionMs",
              static_cast<unsigned long>(config.bbwProtectionMs));
  delta.field("config.scaleLossFixedS", config.scaleLossFixedMs / 1000.0f);
  delta.field("config.doseG", goalWeightDgToGrams(activePreset.doseDg));
  delta.field("config.grindHintsEnabled", bool(activePreset.grindHintsEnabled));
  delta.field("config.touchStopFallbackEnabled",
              bool(config.touchStopFallbackEnabled));
  delta.field("config.cupRemovedWeightG", config.cupRemovedWeightG);
  delta.field("config.bookooMuteOnBuzzerOnly",
              bool(config.bookooMuteOnBuzzerOnly));
  delta.field("config.bookooConnectBeepLevel", "%u",
              static_cast<unsigned>(config.bookooConnectBeepLevel));
  delta.field("config.noScaleAllowRinseWhileArmed",
              bool(noScaleAllowRinseWhileArmed(config.noScaleBbwMode)));
  delta.field("config.lastShotCooldownMs",
              static_cast<unsigned long>(config.lastShotCooldownMs));
  delta.field("config.bbwAlgorithm", "\"%s\"",
              bbwAlgorithmName(static_cast<uint8_t>(BbwAlgorithm::LINEAR_EWMA)));
  delta.field("config.bbwPresetId", "%u",
              static_cast<unsigned>(control.bbwPresetId));
  delta.field("config.bbwEwmaOffsetG",
              std::isfinite(control.bbwEwmaOffsetG) ? "%.2f" : "null",
              static_cast<double>(control.bbwEwmaOffsetG));
  delta.field("config.bbwAlpha", std::isfinite(control.bbwAlpha / 100.0) ? "%.2f" : "null",
              control.bbwAlpha / 100.0);
  delta.field("config.bbwAlphaBaseline",
              std::isfinite(control.bbwAlphaBaseline / 100.0) ? "%.2f" : "null",
              control.bbwAlphaBaseline / 100.0);
  delta.field("config.bbwAlphaSource", "\"%s\"",
              control.bbwAlphaLearned ? "learned" : "initial");
  delta.field("config.bbwEvidenceCount", "%u",
              static_cast<unsigned>(control.bbwEvidenceCount));
  {
    // Drip chart: the REST builder's exact bytes, sliced at its fixed field
    // boundaries into the four client paths.
    char drip[256];
    if (formatDripChartJson(drip, sizeof(drip), control) == 0) return false;
    char *const afterSet = strstr(drip, ",\"bbwRecentAfterSet\":");
    char *const afterQuick = strstr(drip, ",\"bbwRecentAfterQuick\":");
    char *const recent = strstr(drip, ",\"bbwRecentG\":");
    if (!afterSet || !afterQuick || !recent) return false;
    *afterSet = *afterQuick = *recent = '\0';
    delta.field("config.bbwQuickStart", "%s",
                drip + strlen(",\"bbwQuickStart\":"));
    delta.field("config.bbwRecentAfterSet", "%s",
                afterSet + strlen(",\"bbwRecentAfterSet\":"));
    delta.field("config.bbwRecentAfterQuick", "%s",
                afterQuick + strlen(",\"bbwRecentAfterQuick\":"));
    delta.field("config.bbwRecentG", "%s", recent + strlen(",\"bbwRecentG\":"));
  }
  {
    // The melody copy gates the sanitize pass (the renderedPresets pattern);
    // the quoted field form keeps an empty melody a valid "" JSON value.
    if (callbacks_.copyBullseyeConfig)
      callbacks_.copyBullseyeConfig(&g_work->bullseyeMelody);
    else memset(&g_work->bullseyeMelody, 0, sizeof(g_work->bullseyeMelody));
    if (memcmp(&g_work->renderedBullseye, &g_work->bullseyeMelody,
               sizeof(g_work->renderedBullseye)) != 0) {
      g_work->renderedBullseye = g_work->bullseyeMelody;
      sanitizeJsonEmbed(g_work->bullseyeMelody.rtttl, g_work->safeBullseyeRtttl,
                        sizeof(g_work->safeBullseyeRtttl));
    }
    delta.field("config.bullseyeMelodyEnabled", bool(g_work->bullseyeMelody.enabled));
    delta.field("config.bullseyeRtttl", "\"%s\"", g_work->safeBullseyeRtttl);
  }
  {
    const uint32_t historyHash = crc32Update(0xFFFFFFFFU,
        reinterpret_cast<const uint8_t *>(g_work->scaleHistory),
        sizeof(g_work->scaleHistory));
    // Home's escapes reuse the historyJson scratch every dispatch, so the
    // render cache rebuilds on any change and on every snapshot; the sender
    // is buildScaleHistoryJson's caller now (audit F3). The cache is global
    // while the scale.history fingerprint is per-session: sound only because
    // dispatch sends exactly the one bound active session (bind supersedes
    // every other bound session), so no second session's per-session hash can
    // ever be compared against this cache's render.
    if (initial || historyHash != g_work->renderedScaleHistoryHash) {
      buildScaleHistoryJson(g_work->scaleHistory);
      g_work->renderedScaleHistoryHash = historyHash;
    }
    delta.hashedField("scale.history", historyHash, g_work->historyJson);
  }
  // The projection text's live inputs (D14 consumers: applySettingsStatus);
  // Home carries no scale-loss path, so the overlay keeps them.
  delta.field("scaleLoss.projectedS", control.autoToManualGuardTrendMs / 1000.0f);
  delta.field("scaleLoss.source", "%s",
              shotLogScaleLossSourceJson(static_cast<uint8_t>(
                  control.scaleLossProjectedSource << 3)));
#if OPEN_BREW_BY_WEIGHT_MACHINE_INTEGRATION == OPEN_BREW_BY_WEIGHT_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD
  {
    // Consumer-first (D14): every path here has a named reader in the settings
    // model (applyLineaMicraStatus); the live pill state Home already carries
    // (power, readiness, quality, optimistic flags) is not duplicated here.
    // Unconsumed Micra telemetry — sample ages, freshness and transport
    // internals, temperature-command state — rides the Diagnostic projection
    // only; none of it self-ticks here.
    const auto micra = machineIntegrationStatus();
    const auto micraDiscovery = machineIntegrationDiscovery();
    const auto micraWs = machineIntegrationWebSocketStatus(false);
    char safeEmail[LINEA_MICRA_USERNAME_CAPACITY] = {};
    char safeSerial[LINEA_MICRA_SERIAL_CAPACITY] = {};
    char safeName[LINEA_MICRA_NAME_CAPACITY] = {};
    uint8_t options, scaleOptions;
    {
      TaskLockGuard lock(dataMux_);
      options = settings_.lineaMicra.options;
      scaleOptions = settings_.lineaMicra.scaleOptions;
      memcpy(safeEmail, settings_.lineaMicra.username, sizeof(safeEmail));
      memcpy(safeSerial, settings_.lineaMicra.selectedSerial, sizeof(safeSerial));
      memcpy(safeName, settings_.lineaMicra.selectedName, sizeof(safeName));
    }
    sanitizeJsonEmbed(safeEmail, safeEmail, sizeof(safeEmail));
    sanitizeJsonEmbed(safeSerial, safeSerial, sizeof(safeSerial));
    sanitizeJsonEmbed(safeName, safeName, sizeof(safeName));
    delta.field("lineaMicra.email", "\"%s\"", safeEmail);
    delta.field("lineaMicra.selectedSerial", "\"%s\"", safeSerial);
    delta.field("lineaMicra.selectedName", "\"%s\"", safeName);
    delta.field("lineaMicra.applyTemperature",
                bool(options & LINEA_MICRA_APPLY_TEMPERATURE));
    delta.field("lineaMicra.recognizeWakeGesture",
                bool(options & LINEA_MICRA_RECOGNIZE_WAKE));
    delta.field("lineaMicra.powerOnWithScale",
                bool(options & LINEA_MICRA_POWER_ON_WITH_SCALE));
    delta.field("lineaMicra.shutdownWithScale",
                bool(options & LINEA_MICRA_SHUTDOWN_WITH_SCALE));
    delta.field("lineaMicra.shutdownGraceSeconds", "%u",
                static_cast<unsigned>(
                    lineaMicraShutdownGraceSeconds(options)));
    delta.field("lineaMicra.scaleOffWithMachine",
                bool(scaleOptions & LINEA_MICRA_SCALE_OFF_WITH_MACHINE));
    delta.field("lineaMicra.boostScaleDetectionWhenOn",
                bool(scaleOptions & LINEA_MICRA_SCALE_SCAN_BOOST_WHEN_ON));
    delta.field("lineaMicra.reduceScaleScanningWhenOff",
                bool(scaleOptions & LINEA_MICRA_SCALE_SCAN_RELAX_WHEN_OFF));
    delta.field("lineaMicra.phase", "\"%s\"", lineaMicraPhaseName(micra.phase));
    delta.field("lineaMicra.error", "\"%s\"", lineaMicraErrorName(micra.error));
    delta.field("lineaMicra.machineOffline",
                bool(micraWs.machineConnectedKnown && !micraWs.machineConnected));
    delta.field("lineaMicra.targetValid", bool(micra.targetValid));
    delta.field("lineaMicra.targetDeciC", "%u",
                static_cast<unsigned>(micra.targetDeciC));
    delta.field("lineaMicra.staConnected", bool(micra.staConnected));
    delta.field("lineaMicra.apActive", bool(micra.apActive));
    delta.field("lineaMicra.shotPaused", bool(micra.shotPaused));
    delta.field("lineaMicra.machineListing", bool(micraDiscovery.listing));
    delta.field("lineaMicra.machineListError", "\"%s\"",
                lineaMicraErrorName(micraDiscovery.error));
    // The machines array rides the same object; the hash covers the emitted
    // bytes, and the historyJson scratch is free again after scale.history.
    size_t used = 0;
    g_work->historyJson[used++] = '[';
    for (uint8_t index = 0; index < micraDiscovery.count &&
                            index < LINEA_MICRA_MAX_ACCOUNT_MACHINES; ++index) {
      char safeMachineSerial[LINEA_MICRA_SERIAL_CAPACITY] = {};
      char safeMachineName[LINEA_MICRA_NAME_CAPACITY] = {};
      sanitizeJsonEmbed(micraDiscovery.machines[index].serial, safeMachineSerial,
                        sizeof(safeMachineSerial));
      sanitizeJsonEmbed(micraDiscovery.machines[index].name, safeMachineName,
                        sizeof(safeMachineName));
      const int n = snprintf(g_work->historyJson + used,
                             NetworkWorkBuf::kHistoryJson - used,
                             "%s{\"serial\":\"%s\",\"name\":\"%s\"}",
                             index == 0 ? "" : ",", safeMachineSerial,
                             safeMachineName);
      if (n < 0 || static_cast<size_t>(n) >=
                       NetworkWorkBuf::kHistoryJson - used)
        break;
      used += static_cast<size_t>(n);
    }
    // buildScaleHistoryJson's terminator guard: an exactly full buffer must
    // not write its NUL one byte past historyJson.
    if (used + 1 < NetworkWorkBuf::kHistoryJson) {
      g_work->historyJson[used++] = ']';
      g_work->historyJson[used] = '\0';
    }
    delta.field("lineaMicra.machines", "%s", g_work->historyJson);
  }
#endif
  if (!delta.ok || !statusJsonAppend(&delta.used, "}}")) return false;
  if (!delta.changed) return true;
  // The delta hashes were already consumed for a deferred frame, so only a
  // full snapshot can repair the client.
  bool deferred;
  if (!sendUiStreamFrame(session, delta.used, deferred)) return false;
  if (deferred) {
    session.settingsResync = true;
    return true;
  }
  session.settingsBoot = control.bootId;
  session.settingsResync = false;
  return true;
}
