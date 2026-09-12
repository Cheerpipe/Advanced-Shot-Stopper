void rejectWebCommand(const WebCommand &command) {
  addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_REJECTED,
                static_cast<int32_t>(command.type));
  reportControlCommandResult(command, CommandResultState::FAILED);
}
bool persistBleCompanionEnabled(bool enabled);
bool persistBleScanIntensity(BleScanIntensity intensity);

void processWebCommand(const WebCommand &command) {
  switch (command.type) {
    case WebCommandType::STOP:
    case WebCommandType::STOP_HEARTBEAT:
    case WebCommandType::REMOTE_OFF:
      if (!machineIsRunning() ||
          (command.type == WebCommandType::REMOTE_OFF &&
           (!session.active || session.source != ControlSource::WEB))) {
        rejectWebCommand(command);
        return;
      }
      machineNoteFirmwareStop();
      addDebugEvent(DebugCategory::WEB,
                    command.type == WebCommandType::REMOTE_OFF
                        ? DebugCode::WEB_REMOTE_OFF
                        : DebugCode::WEB_STOP,
                    static_cast<int32_t>(command.type),
                    static_cast<int32_t>(session.id));
      if (!session.active) {
        const bool applied = machineRequestWebStop();
        reportControlCommandResult(
            command, applied ? CommandResultState::APPLIED
                             : CommandResultState::FAILED);
        return;
      }
      {
        const bool applied = finalizeCycle(
          command.type == WebCommandType::STOP_HEARTBEAT
              ? EndReason::WEB_HEARTBEAT_TIMEOUT
              : EndReason::WEB_STOP,
          nextStateForUserHold(machineLastIntention()), true);
        reportControlCommandResult(
            command, applied ? CommandResultState::APPLIED
                             : CommandResultState::FAILED);
      }
      return;

    case WebCommandType::FORCE_SWITCH_PULSE:
      if (!REMOTE_MACHINE_CONTROL_ENABLED || SHOT_STOPPER_MACHINE_TYPE == 0) {
        rejectWebCommand(command);
        return;
      }
      if (machineRequestForcedPulse()) {
        addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_ACCEPTED,
                      static_cast<int32_t>(command.type));
        reportControlCommandResult(command, CommandResultState::APPLIED);
      } else {
        rejectWebCommand(command);
      }
      return;

    case WebCommandType::STATE_OVERRIDE_OFF:
      machineOverrideInferredOff();
      if (session.active) {
        finalizeCycle(EndReason::WEB_STOP,
                      nextStateForUserHold(machineLastIntention()));
      }
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_ACCEPTED,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::STATE_OVERRIDE_ON:
      machineOverrideInferredOn();
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_ACCEPTED,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::REMOTE_ON:
      if (!REMOTE_MACHINE_CONTROL_ENABLED || !webCommandAllowsUnsafeConfiguration(command)) {
        rejectWebCommand(command);
        return;
      }
      virtualHoldOn = true;
      beginCycle(ControlSource::WEB);
      if (!session.active) {
        virtualHoldOn = false;
        reportControlCommandResult(command, CommandResultState::FAILED);
        return;
      }
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_REMOTE_ON,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::RINSE:
      if (!machineSupportsRinse() || !REMOTE_MACHINE_CONTROL_ENABLED ||
          !webCommandAllowsUnsafeConfiguration(command)) {
        rejectWebCommand(command);
        return;
      }
      beginWebRinse();
      if (!session.active) {
        reportControlCommandResult(command, CommandResultState::FAILED);
        return;
      }
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_RINSE,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::APPLY_CONFIG: {
      if (!persistentConfigurationMutationAllowed(command)) {
        rejectWebCommand(command);
        return;
      }
      RecipeSnapshot quickBefore;
      quickBefore.presets = presetBank;
      quickBefore.runtime = runtimeConfig;
      BullseyeMelodyConfig stagedBullseye = {};
      if (command.bullseyeConfigSpecified &&
          (!takeStagedBullseyeConfig(command.bullseyeStageRequestId,
                                     stagedBullseye) ||
           !validBullseyeMelodyConfig(stagedBullseye))) {
        rejectWebCommand(command);
        return;
      }
      // Machine/workflow fields only. Recipe authority stays on the active preset.
      RuntimeConfig candidate = runtimeConfig;
      candidate.autoTare = command.config.autoTare;
      candidate.autoTareOutsideBrew = command.config.autoTareOutsideBrew;
      candidate.postTareBaselineGraceMs = command.config.postTareBaselineGraceMs;
      candidate.canTareStartTimer = command.config.canTareStartTimer;
      candidate.scaleTimerStopExtraDelayMs =
          command.config.scaleTimerStopExtraDelayMs;
      candidate.dripDelayMs = command.config.dripDelayMs;
      candidate.soundAlertsMuted = command.config.soundAlertsMuted;
      candidate.firstDropBeep = command.config.firstDropBeep;
      machineApplyWorkflowConfig(candidate, command.config);
      candidate.rinseDurationMs = command.config.rinseDurationMs;
      candidate.rinseEnabled = command.config.rinseEnabled;
      candidate.buzzerScaleLostBeep = command.config.buzzerScaleLostBeep;
      candidate.buzzerAutoToManualGuardEndBeep =
          command.config.buzzerAutoToManualGuardEndBeep;
      candidate.buzzerManualNoScaleBeep = command.config.buzzerManualNoScaleBeep;
      candidate.buzzerScaleConnectedBeep =
          command.config.buzzerScaleConnectedBeep;
      candidate.scaleConnectedLed = command.config.scaleConnectedLed;
      candidate.buzzerExtendedPulseRate = command.config.buzzerExtendedPulseRate;
      candidate.buzzerSlowExtendedPulseRate =
          command.config.buzzerSlowExtendedPulseRate;
      candidate.alertOutputChannel = command.config.alertOutputChannel;
      candidate.bookooMuteOnBuzzerOnly = command.config.bookooMuteOnBuzzerOnly;
      candidate.bookooConnectBeepLevel = command.config.bookooConnectBeepLevel;
      candidate.autoRetare = command.config.autoRetare;
      candidate.retareWindowMs = command.config.retareWindowMs;
      candidate.minimumCupWeightG = command.config.minimumCupWeightG;
      candidate.cupRemovedWeightG = command.config.cupRemovedWeightG;
      candidate.retareStabilitySamples = command.config.retareStabilitySamples;
      candidate.retareStabilityToleranceG =
          command.config.retareStabilityToleranceG;
      candidate.retareStabilityMaxGapMs = command.config.retareStabilityMaxGapMs;
      candidate.retareStabilityMinDurationMs =
          command.config.retareStabilityMinDurationMs;
      candidate.timezoneOffsetMinutes = command.config.timezoneOffsetMinutes;
      candidate.ntpServerPreset = command.config.ntpServerPreset;
      memcpy(candidate.ntpServerCustom, command.config.ntpServerCustom,
             sizeof(candidate.ntpServerCustom));
      if (candidate.scaleMacCacheMode != command.config.scaleMacCacheMode) {
        serialTracef(LogLevel::INFO, "Scale preference: %s",
                     scaleMacCacheModeId(command.config.scaleMacCacheMode));
      }
      candidate.scaleMacCacheMode = command.config.scaleMacCacheMode;
      candidate.noScaleBbwMode = command.config.noScaleBbwMode;
      candidate.lastShotCooldownMs = command.config.lastShotCooldownMs;
      candidate.serialLogLevel = command.config.serialLogLevel;
      candidate.ringRetainLogLevel = command.config.ringRetainLogLevel;
      candidate.showDiagnosticPage = command.config.showDiagnosticPage;
      candidate.powerManagementEnabled = command.config.powerManagementEnabled;
      // Session Manual switch may arrive via brewByWeight on Home; keep as timerOnly.
      candidate.timerOnly = command.config.timerOnly;
#if defined(SHOT_STOPPER_HOST_TEST)
      // Host APPLY_CONFIG still carries brew fields; update the active recipe.
      {
        ensureShotPresetBank(presetBank, candidate.retareWindowMs,
                             candidate.autoRetare);
        ShotPreset &preset = mutableActiveShotPreset(presetBank);
        copyUserRecipeFromConfig(command.config, preset);
      }
#endif
      RuntimeConfig composed = composeEffectiveConfig(candidate, presetBank);
      if (validateRuntimeConfig(composed) != ConfigValidationError::NONE) {
        addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        rejectWebCommand(command);
        return;
      }
      composed.revision = runtimeConfig.revision + 1;
      if (composed.revision == 0) {
        composed.revision = 1;
      }
      ensureShotPresetBank(presetBank, composed.retareWindowMs,
                           composed.autoRetare);
      if (candidate.dripDelayMs != runtimeConfig.dripDelayMs ||
          candidate.autoTare != runtimeConfig.autoTare ||
          candidate.autoRetare != runtimeConfig.autoRetare ||
          candidate.retareWindowMs != runtimeConfig.retareWindowMs ||
          candidate.postTareBaselineGraceMs != runtimeConfig.postTareBaselineGraceMs) {
        for (uint8_t i = 0; i < presetBank.count; ++i)
          bbwLearningBank.invalidate(presetBank.presets[i].id, presetBank, 1);
      }
      commitLiveRuntimeConfig(composed, RUNTIME_PERSIST_REASON_USER);
      RecipeSnapshot quickAfter;
      quickAfter.presets = presetBank;
      quickAfter.runtime = runtimeConfig;
      quickSettingsPersistPending |= !quickSettingsEqual(
          quickSettingsSnapshot(quickBefore), quickSettingsSnapshot(quickAfter));
      if (command.bullseyeConfigSpecified) {
        commitLiveBullseyeConfig(stagedBullseye);
      }
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
    }

    case WebCommandType::QUICK_SETTING: {
      if (pendingPresetPersistence.requestId != 0 ||
          !persistentConfigurationMutationAllowed(command) ||
          command.config.revision != runtimeConfig.revision) {
        rejectWebCommand(command);
        return;
      }
      RecipeSnapshot before;
      before.presets = presetBank;
      before.runtime = runtimeConfig;
      RuntimeConfig candidateRuntime = runtimeConfig;
      ShotPresetBank candidateBank = presetBank;
      ShotPreset &preset = mutableActiveShotPreset(candidateBank);
      const auto quickField = static_cast<QuickSettingField>(command.presetAction);
      const bool enabled = command.presetId != 0;
      if (quickField > QuickSettingField::CUP_PROTECTION ||
          (quickField != QuickSettingField::NO_SCALE_BBW_MODE &&
           command.presetId > 1)) {
        rejectWebCommand(command);
        return;
      }
      switch (quickField) {
        case QuickSettingField::BREW_BY_WEIGHT:
          candidateRuntime.timerOnly = !enabled;
          break;
        case QuickSettingField::NO_SCALE_BBW_MODE:
          if (!validNoScaleBbwMode(command.presetId)) {
            rejectWebCommand(command);
            return;
          }
          candidateRuntime.noScaleBbwMode = command.presetId;
          break;
        case QuickSettingField::AUTO_TO_MANUAL_GUARD:
          preset.autoToManualGuardEnabled = enabled;
          break;
        case QuickSettingField::SLOW_EXTRACTION_GUARD:
          preset.slowExtractionGuardEnabled = enabled;
          break;
        case QuickSettingField::FAST_EXTRACTION_GUARD:
          preset.fastExtractionGuardEnabled = enabled;
          break;
        case QuickSettingField::AVOID_ACCIDENTAL_TOUCH:
          preset.avoidAccidentalTouchEnabled = enabled;
          break;
        case QuickSettingField::CUP_PROTECTION:
          preset.cupProtectionEnabled = enabled;
          break;
      }
      RuntimeConfig composed = composeEffectiveConfig(candidateRuntime, candidateBank);
      if (validateRuntimeConfig(composed) != ConfigValidationError::NONE ||
          !validateShotPresetBank(candidateBank, candidateRuntime.retareWindowMs,
                                  candidateRuntime.autoRetare)) {
        rejectWebCommand(command);
        return;
      }
      RecipeSnapshot after;
      after.presets = candidateBank;
      after.runtime = composed;
      if (quickSettingsEqual(quickSettingsSnapshot(before),
                             quickSettingsSnapshot(after))) {
        reportControlCommandResult(command, CommandResultState::PERSISTED);
        return;
      }
      presetBank = candidateBank;
      composed.revision = runtimeConfig.revision + 1;
      if (composed.revision == 0) composed.revision = 1;
      commitLiveRuntimeConfig(composed, RUNTIME_PERSIST_REASON_USER);
      quickSettingsPersistPending = true;
      pendingPresetPersistence = {command.requestId, composed.revision, false};
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
    }

    case WebCommandType::RESET_WEIGHT_OFFSET: {
      if (!persistentConfigurationMutationAllowed(command) ||
          (command.bbwResetRevisionSpecified &&
           command.config.revision != runtimeConfig.revision)) {
        rejectWebCommand(command);
        return;
      }
      ShotPreset &preset = mutableActiveShotPreset(presetBank);
      if (command.bbwFullReset && preset.bbwAlgorithm != 1) {
        rejectWebCommand(command);
        return;
      }
      bbwLearningBank.invalidate(preset.id, presetBank, preset.bbwAlgorithm);
      (preset.bbwAlgorithm == 0 ? preset.weightOffsetG : preset.bbwEwmaOffsetG) =
          preset.weightOffsetBaselineG;
      if (command.bbwFullReset) {
        preset.bbwEwmaAlpha = preset.bbwAlphaBaseline;
        preset.bbwAlphaLearned = 0;
      }
      RuntimeConfig candidate = runtimeConfig;
      applyShotPresetToConfig(preset, candidate, true);
      ++candidate.revision;
      if (candidate.revision == 0) {
        candidate.revision = 1;
      }
      commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_USER);
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
    }

    case WebCommandType::RESET_AUTO_TO_MANUAL_GUARD_SAMPLES: {
      if (!persistentConfigurationMutationAllowed(command)) {
        rejectWebCommand(command);
        return;
      }
      pendingFinalize.pending = false;
      ShotPreset &preset = mutableActiveShotPreset(presetBank);
      resetAutoToManualGuardSamples(preset.autoToManualGuardSamplesDs,
                                    preset.autoToManualGuardBaselineMs);
      RuntimeConfig candidate = runtimeConfig;
      applyShotPresetToConfig(preset, candidate, true);
      ++candidate.revision;
      if (candidate.revision == 0) {
        candidate.revision = 1;
      }
      addDebugEvent(DebugCategory::CONFIG,
                    DebugCode::AUTO_TO_MANUAL_GUARD_SAMPLES_RESET);
      commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_USER);
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
    }

    case WebCommandType::PRESET_OP: {
      if (pendingPresetPersistence.requestId != 0) {
        rejectWebCommand(command);
        return;
      }
      if (!persistentConfigurationMutationAllowed(command)) {
        rejectWebCommand(command);
        return;
      }
      RecipeSnapshot quickBefore;
      quickBefore.presets = presetBank;
      quickBefore.runtime = runtimeConfig;
      bool ok = false;
      uint8_t newId = 0;
      const PresetAction action = static_cast<PresetAction>(command.presetAction);
      switch (action) {
        case PresetAction::APPLY:
          ok = setActiveShotPreset(presetBank, command.presetId);
          break;
        case PresetAction::SAVE: {
          ShotPreset *preset = mutableShotPreset(presetBank, command.presetId);
          if (preset == nullptr) {
            ok = false;
            break;
          }
          ShotPreset candidateRecipe = *preset;
          copyUserRecipeFromConfig(command.config, candidateRecipe);
          if (!command.bbwAlgorithmSpecified)
            candidateRecipe.bbwAlgorithm = preset->bbwAlgorithm;
          if (!command.bbwBaselineSpecified)
            candidateRecipe.weightOffsetBaselineG = preset->weightOffsetBaselineG;
          if (command.bbwAlphaBaseline != 0)
            candidateRecipe.bbwAlphaBaseline = command.bbwAlphaBaseline;
          // Form sends brewByWeight explicitly; do not use session Manual.
          candidateRecipe.brewByWeight = !command.config.timerOnly;
          ok = validateShotPresetRecipe(candidateRecipe,
                                        runtimeConfig.retareWindowMs,
                                        runtimeConfig.autoRetare);
          if (ok) {
            ShotPreset previous = *preset;
            previous.bbwAlgorithm = candidateRecipe.bbwAlgorithm;
            previous.weightOffsetBaselineG = candidateRecipe.weightOffsetBaselineG;
            previous.bbwAlphaBaseline = candidateRecipe.bbwAlphaBaseline;
            if (memcmp(&previous, &candidateRecipe, sizeof(previous)) != 0)
              bbwLearningBank.invalidate(preset->id, presetBank);
            // Preserve learned fields from the live preset.
            candidateRecipe.weightOffsetG = preset->weightOffsetG;
            memcpy(candidateRecipe.autoToManualGuardSamplesDs,
                   preset->autoToManualGuardSamplesDs,
                   sizeof(candidateRecipe.autoToManualGuardSamplesDs));
            *preset = candidateRecipe;
            presetBank.activeId = preset->id;
          }
          break;
        }
        case PresetAction::CREATE:
          ok = createUntitledShotPreset(presetBank, newId);
          break;
        case PresetAction::DELETE:
          ok = deleteShotPreset(presetBank, command.presetId);
          if (ok) {
            for (auto &state : bbwLearningBank.states)
              if (state.id == command.presetId) state = BbwLearningBank::State{};
          }
          break;
        case PresetAction::DUPLICATE:
          ok = duplicateShotPreset(presetBank, command.presetId, newId);
          break;
        case PresetAction::RENAME:
          ok = renameShotPreset(presetBank, command.presetId, command.presetName);
          break;
        case PresetAction::RESTORE_FACTORY_VALUES:
          ok = restoreFactoryShotPresetValues(presetBank, command.presetId);
          if (ok) bbwLearningBank.invalidate(command.presetId, presetBank);
          break;
      }
      if (!ok) {
        addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED,
                      static_cast<int32_t>(command.presetAction));
        rejectWebCommand(command);
        return;
      }
      ensureShotPresetBank(presetBank, runtimeConfig.retareWindowMs,
                           runtimeConfig.autoRetare);
      RuntimeConfig candidate = runtimeConfig;
      applyShotPresetToConfig(activeShotPreset(presetBank), candidate, true);
      ++candidate.revision;
      if (candidate.revision == 0) {
        candidate.revision = 1;
      }
      commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_USER);
      RecipeSnapshot quickAfter;
      quickAfter.presets = presetBank;
      quickAfter.runtime = runtimeConfig;
      quickSettingsPersistPending |= !quickSettingsEqual(
          quickSettingsSnapshot(quickBefore), quickSettingsSnapshot(quickAfter));
      pendingPresetPersistence = {command.requestId, candidate.revision, true};
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
    }

    case WebCommandType::RESTART:
      holdOrBeginPlannedRestart(command);
      return;

    case WebCommandType::SAVE_NETWORK:
    case WebCommandType::SAVE_WEBHOOK:
    case WebCommandType::FORGET_NETWORK:
    case WebCommandType::CHANGE_DEVICE_PASSWORD:
    case WebCommandType::RESET_DEVICE_PASSWORD:
    case WebCommandType::RESET_NETWORK_AP:
    case WebCommandType::FACTORY_RESET:
      if (!webCommandAllowsUnsafeConfiguration(command)) {
        rejectWebCommand(command);
        return;
      }
      if (!beginMaintenanceLease(command, false)) {
        rejectWebCommand(command);
      }
      return;

    case WebCommandType::PERSIST_RUNTIME:
      rejectWebCommand(command);
      return;

    case WebCommandType::CLEAR_SHOT_LOG:
      rejectWebCommand(command);
      return;

    case WebCommandType::CLEAR_RESET_HISTORY:
      if (!clearPersistedResetHistory(safetyResetStatus.unsafeResetCount)) {
        rejectWebCommand(command);
        return;
      }
      safetyResetStatus.resetHistoryCount = 0;
      for (ResetHistoryEntry &entry : safetyResetStatus.resetHistory) {
        entry = ResetHistoryEntry{};
      }
      reportControlCommandResult(command, CommandResultState::PERSISTED);
      return;

    case WebCommandType::CLEAR_PREFERRED_SCALE:
      if (!persistentConfigurationMutationAllowed(command)) {
        rejectWebCommand(command);
        return;
      }
      clearPreferredScaleCache();
      servicePreferredScaleMacPersistence();
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::SELECT_PREFERRED_SCALE:
      if (!persistentConfigurationMutationAllowed(command)) {
        rejectWebCommand(command);
        return;
      }
      if (command.network.scaleSelectMac[0] == '\0') {
        selectPreferredScale("", "");
      } else if (!validPreferredScaleMac(command.network.scaleSelectMac)) {
        rejectWebCommand(command);
        return;
      } else {
        selectPreferredScale(command.network.scaleSelectMac, command.network.scaleSelectName);
      }
      servicePreferredScaleMacPersistence();
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::START_WIFI_SCAN:
      if (!beginMaintenanceLease(command, false)) {
        rejectWebCommand(command);
      }
      return;

    case WebCommandType::BUZZER_TEST: {
      if (!webCommandAllowsUnsafeConfiguration(command)) {
        rejectWebCommand(command);
        return;
      }
      bool accepted = false;
      if (command.bullseyeConfigSpecified) {
        BullseyeMelodyConfig testBullseye = {};
        if (!takeStagedBullseyeConfig(command.bullseyeStageRequestId,
                                      testBullseye) ||
            !validBullseyeRtttl(testBullseye.rtttl)) {
          rejectWebCommand(command);
          return;
        }
        accepted = localBuzzer.configureBullseyeRtttl(testBullseye.rtttl) &&
                   localBuzzer.requestBullseye(/*allowQueue=*/false);
        // An accepted test has already copied its notes to the active playback
        // buffer. A busy finite cue rejects the test instead of queueing a cue
        // whose source would change when the live tune is restored. The live
        // config has a single writer on this control task, so no stack copy is
        // needed here.
        (void)localBuzzer.configureBullseyeRtttl(
            bullseyeMelodyConfig.rtttl);
      } else {
        accepted = buzzerPatternIsPulseTrain(command.buzzerPattern)
                       ? startPulseTrain(command.buzzerPattern,
                                         BUZZER_PULSE_TRAIN_DEBUG_MS)
                       : localBuzzer.request(command.buzzerPattern);
      }
      if (!accepted) {
        rejectWebCommand(command);
        return;
      }
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_ACCEPTED,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
    }

    case WebCommandType::BOOKOO_DEBUG: {
      if (!controlAllowsConfigurationNow()) {
        rejectWebCommand(command);
        return;
      }
      if (!enqueueScaleDebugCommand(command.bookooDebugAction,
                                    command.bookooBeepLevel)) {
        rejectWebCommand(command);
        return;
      }
      addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_ACCEPTED,
                    static_cast<int32_t>(command.type));
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
    }

    case WebCommandType::MAINTENANCE_COMPLETE:
      completeMaintenanceLease(command);
      return;

    case WebCommandType::WIFI_CONNECT:
    case WebCommandType::WIFI_DISCONNECT:
    case WebCommandType::WIFI_RESTART:
    case WebCommandType::AP_START:
    case WebCommandType::AP_STOP:
    case WebCommandType::WEBUI_START:
    case WebCommandType::WEBUI_STOP:
    case WebCommandType::WEBUI_RESTART:
      if (!forwardAcceptedNetworkCommand(command)) {
        rejectWebCommand(command);
      }
      return;

    case WebCommandType::BLE_COMPAT_ENABLE:
    case WebCommandType::BLE_COMPAT_DISABLE: {
      const bool enabled =
          command.type == WebCommandType::BLE_COMPAT_ENABLE;
      if (!persistBleCompanionEnabled(enabled)) {
        rejectWebCommand(command);
        return;
      }
      if (command.bleScanIntensitySpecified &&
          !persistBleScanIntensity(clampBleScanIntensity(
              command.bleScanIntensity))) {
        rejectWebCommand(command);
        return;
      }
      addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED,
                    enabled ? 1 : 0);
      reportControlCommandResult(command, CommandResultState::PERSISTED);
      return;
    }

    case WebCommandType::BLE_SCAN_INTENSITY: {
      if (!persistBleScanIntensity(clampBleScanIntensity(
              command.bleScanIntensity))) {
        rejectWebCommand(command);
        return;
      }
      addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED,
                    static_cast<int32_t>(command.bleScanIntensity));
      reportControlCommandResult(command, CommandResultState::PERSISTED);
      return;
    }

    case WebCommandType::TASK_PROFILER_START:
      (void)taskProfiler.start(millis());
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;

    case WebCommandType::TASK_PROFILER_STOP:
      (void)taskProfiler.stop(millis());
      reportControlCommandResult(command, CommandResultState::APPLIED);
      return;
  }
}

void processWebCommands() {
  if (webCommandQueue == nullptr || controlResultPending ||
      maintenanceCancellationPending) {
    return;
  }
  WebCommand command;
  if (xQueueReceive(webCommandQueue, &command, 0) == pdTRUE) {
    processWebCommand(command);
  }
}

void reportBleCompanionResult(const BleCompanionRequest &request,
                              bool accepted,
                              BleCompanionRejectReason reason) {
  BleCompanionResult result;
  result.sequence = request.sequence;
  result.accepted = accepted;
  result.reason = accepted ? BleCompanionRejectReason::NONE : reason;
  if (bleCompanionResultQueue != nullptr) {
    if (xQueueSend(bleCompanionResultQueue, &result, 0) != pdTRUE) {
      ++bleCompanionResultDropped;
      addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_EVENT_DROPPED,
                    static_cast<int32_t>(request.type));
      logEmit(LogLevel::WARNING, DebugCategory::SCALE,
              DebugCode::SCALE_EVENT_DROPPED,
              static_cast<int32_t>(request.sequence),
              static_cast<int32_t>(request.type));
    }
  }
}

bool persistBleScanIntensity(BleScanIntensity intensity) {
  intensity = clampBleScanIntensity(static_cast<uint8_t>(intensity));
#if !defined(SHOT_STOPPER_HOST_TEST)
  const uint8_t stored = static_cast<uint8_t>(intensity);
  if (bleCompanionPersistedSettings.scanIntensity != stored) {
    BleCompanionPersistedSettings candidate = bleCompanionPersistedSettings;
    candidate.scanIntensity = stored;
    if (!saveBleCompanionSettings(candidate)) {
      return false;
    }
    bleCompanionPersistedSettings = candidate;
  }
#endif
  applyLiveBleScanIntensity(intensity);
  return true;
}

bool persistBleCompanionEnabled(bool enabled) {
#if !defined(SHOT_STOPPER_HOST_TEST)
  if ((bleCompanionPersistedSettings.enabled != 0) != enabled) {
    BleCompanionPersistedSettings candidate = bleCompanionPersistedSettings;
    candidate.enabled = enabled ? 1 : 0;
    if (!saveBleCompanionSettings(candidate)) {
      return false;
    }
    bleCompanionPersistedSettings = candidate;
  }
#endif
  bleCompanionMux.lock();
  bleCompanionStatusSnapshot.configuredEnabled = enabled;
  bleCompanionStatusSnapshot.restartRequired =
      enabled != bleCompanionStatusSnapshot.enabled;
  bleCompanionRuntimeSnapshot.configuredEnabled = enabled;
  bleCompanionMux.unlock();
  return true;
}

bool applyBleCompanionRecipeRequest(const BleCompanionRequest &request) {
  RuntimeConfig candidateRuntime = runtimeConfig;
  ShotPresetBank candidateBank = presetBank;
  ensureShotPresetBank(candidateBank, candidateRuntime.retareWindowMs,
                       candidateRuntime.autoRetare);
  ShotPreset &candidatePreset = mutableActiveShotPreset(candidateBank);
  uint32_t milliseconds = 0;

  switch (request.type) {
    case BleCompanionRequestType::SET_BREW_BY_WEIGHT:
      if (request.value > 1) return false;
      candidatePreset.brewByWeight = request.value != 0;
      break;
    case BleCompanionRequestType::SET_GOAL_WEIGHT:
      candidatePreset.goalWeightG = request.value;
      break;
    case BleCompanionRequestType::SET_AUTO_TARE:
      if (request.value > 1) return false;
      candidateRuntime.autoTare = request.value != 0;
      break;
    case BleCompanionRequestType::SET_BBW_PROTECTION_SECONDS:
      bleCompanionSecondsToMs(request.value, milliseconds);
      candidatePreset.bbwProtectionMs = milliseconds;
      break;
    case BleCompanionRequestType::SET_OPERATIONAL_WALL_SECONDS:
      bleCompanionSecondsToMs(request.value, milliseconds);
      candidatePreset.operationalWallMs = milliseconds;
      break;
    case BleCompanionRequestType::SET_DRIP_DELAY_SECONDS:
      bleCompanionSecondsToMs(request.value, milliseconds);
      candidateRuntime.dripDelayMs = milliseconds;
      break;
    default:
      return false;
  }

  const RuntimeConfig composed =
      composeEffectiveConfig(candidateRuntime, candidateBank);
  if (validateRuntimeConfig(composed) != ConfigValidationError::NONE ||
      !validateShotPresetBank(candidateBank, candidateRuntime.retareWindowMs,
                              candidateRuntime.autoRetare)) {
    return false;
  }
  const RuntimeConfig currentEffective = effectiveRuntimeConfig();
  if (memcmp(&candidateBank, &presetBank, sizeof(candidateBank)) == 0 &&
      memcmp(&composed, &currentEffective, sizeof(composed)) == 0) {
    return true;
  }
  presetBank = candidateBank;
  if (candidateRuntime.autoTare != runtimeConfig.autoTare ||
      candidateRuntime.dripDelayMs != runtimeConfig.dripDelayMs) {
    for (uint8_t i = 0; i < presetBank.count; ++i)
      bbwLearningBank.invalidate(presetBank.presets[i].id, presetBank, 1);
  } else {
    bbwLearningBank.invalidate(presetBank.activeId, presetBank);
  }
  RuntimeConfig committed = composed;
  committed.revision = runtimeConfig.revision + 1;
  if (committed.revision == 0) {
    committed.revision = 1;
  }
  commitLiveRuntimeConfig(committed, RUNTIME_PERSIST_REASON_USER);
  return true;
}

void processBleCompanionRequests() {
  if (bleCompanionRequestQueue == nullptr) {
    return;
  }
  BleCompanionRequest request;
  if (xQueueReceive(bleCompanionRequestQueue, &request, 0) != pdTRUE) {
    return;
  }

  if (!copyBleCompanionStatus().enabled) {
    reportBleCompanionResult(request, false,
                             BleCompanionRejectReason::SUPPORT_DISABLED);
    return;
  }

  if (request.type == BleCompanionRequestType::SET_AP_ENABLED) {
    if (request.value > 1) {
      reportBleCompanionResult(request, false,
                               BleCompanionRejectReason::INVALID_VALUE);
      return;
    }
    WebCommand command;
    command.type = request.value != 0 ? WebCommandType::AP_START
                                      : WebCommandType::AP_STOP;
    command.requestId = request.sequence;
    const bool queued = enqueueWebCommand(command);
    reportBleCompanionResult(
        request, queued, queued ? BleCompanionRejectReason::NONE
                                : BleCompanionRejectReason::QUEUE_FULL);
    return;
  }

  if (!controlAllowsConfigurationNow() || !settingsPersistenceAvailable()) {
    reportBleCompanionResult(request, false,
                             BleCompanionRejectReason::NOT_READY);
    return;
  }

  switch (request.type) {
    case BleCompanionRequestType::SET_BREW_BY_WEIGHT:
    case BleCompanionRequestType::SET_GOAL_WEIGHT:
    case BleCompanionRequestType::SET_AUTO_TARE:
    case BleCompanionRequestType::SET_BBW_PROTECTION_SECONDS:
    case BleCompanionRequestType::SET_OPERATIONAL_WALL_SECONDS:
    case BleCompanionRequestType::SET_DRIP_DELAY_SECONDS: {
      const bool accepted = applyBleCompanionRecipeRequest(request);
      reportBleCompanionResult(
          request, accepted,
          accepted ? BleCompanionRejectReason::NONE
                   : BleCompanionRejectReason::INVALID_VALUE);
      return;
    }
    case BleCompanionRequestType::SAVE_WIFI: {
      if (!validWifiSsid(request.ssid) ||
          !validWifiPassword(request.openNetwork ? "" : request.password,
                             request.openNetwork)) {
        reportBleCompanionResult(request, false,
                                 BleCompanionRejectReason::INVALID_VALUE);
        return;
      }
      WebCommand command;
      command.setNetworkType(WebCommandType::SAVE_NETWORK);
      command.requestId = request.sequence;
      copyCString(command.network.ssid, sizeof(command.network.ssid), request.ssid);
      command.network.openNetwork = request.openNetwork;
      if (!request.openNetwork) {
        copyCString(command.network.password, sizeof(command.network.password),
                    request.password);
      }
      const bool queued = enqueueWebCommand(command);
      command.wipePassword();
      reportBleCompanionResult(
          request, queued, queued ? BleCompanionRejectReason::NONE
                                  : BleCompanionRejectReason::QUEUE_FULL);
      return;
    }
    case BleCompanionRequestType::REBOOT: {
      if (request.value != 1) {
        reportBleCompanionResult(request, false,
                                 BleCompanionRejectReason::INVALID_PAYLOAD);
        return;
      }
      WebCommand command;
      command.type = WebCommandType::RESTART;
      command.requestId = request.sequence;
      const bool queued = enqueueWebCommand(command);
      reportBleCompanionResult(
          request, queued, queued ? BleCompanionRejectReason::NONE
                                  : BleCompanionRejectReason::QUEUE_FULL);
      return;
    }
    case BleCompanionRequestType::SET_AP_ENABLED:
      return;
  }
}
