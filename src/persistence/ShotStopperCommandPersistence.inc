// Web/control bridge
// ---------------------------------------------------------------------------

void maybeRequestNtpSyncOnActivity() {
#ifndef SHOT_STOPPER_HOST_TEST
  networkManager.requestNtpSyncIfNeeded();
#else
  if (wallClockNeedsActivityNtpSync(g_wallClock, hostMillis)) {
    ++hostNtpSyncRequestCount;
  }
#endif
}
bool controlAllowsConfigurationNow() {
  return stopperState == StopperState::READY && !session.active &&
         !machineIsRunning() && !machineLastIntention().holdActive &&
         !maintenanceLease.active;
}

void beginWebRinse() {
  if (!beginRinseCycle(ControlSource::WEB)) {
    return;
  }
  (void)enterRinse();
}

bool forwardAcceptedNetworkCommand(const WebCommand &command) {
#ifndef SHOT_STOPPER_HOST_TEST
  if (!networkManager.enqueueAcceptedCommand(command)) {
    addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_REJECTED,
                  static_cast<int32_t>(command.type));
    return false;
  }
  return true;
#else
  ++hostForwardAcceptedNetworkCommandCalls;
  hostLastForwardedNetworkCommand = command;
  return hostForwardAcceptedNetworkCommandSucceeds;
#endif
}

void reportControlCommandResult(const WebCommand &command,
                                CommandResultState state) {
  if (command.requestId == 0 || controlResultPending) {
    return;
  }
  WebCommand result;
  result.type = WebCommandType::MAINTENANCE_COMPLETE;
  result.requestId = command.requestId;
  result.succeeded = state == CommandResultState::APPLIED ||
                     state == CommandResultState::PERSISTED;
  result.resultState = state;
  if (!forwardAcceptedNetworkCommand(result)) {
    controlResultCommand = result;
    controlResultPending = true;
  }
}

void serviceControlCommandResult() {
  if (controlResultPending &&
      forwardAcceptedNetworkCommand(controlResultCommand)) {
    controlResultPending = false;
    controlResultCommand = WebCommand{};
  }
}

void serviceMaintenanceCancellation() {
  if (maintenanceCancellationPending &&
      forwardAcceptedNetworkCommand(maintenanceCancellationCommand)) {
    maintenanceCancellationPending = false;
    maintenanceCancellationCommand = WebCommand{};
  }
}

bool beginMaintenanceLease(const WebCommand &networkCommand,
                           bool applyRuntimeOnSuccess) {
  if (!controlAllowsConfigurationNow() && !networkCommand.unsafeWebUiOverride) {
    return false;
  }
  // A planned restart must never cut a shot. Override cannot change that.
  if (networkCommand.type == WebCommandType::RESTART &&
      (session.active || getRelaySafetySnapshot().closed ||
       machineLastIntention().holdActive)) {
    return false;
  }
  machineRequestStop();
  maintenanceLease = MaintenanceLease{};
  maintenanceLease.active = true;
  maintenanceLease.applyRuntimeOnSuccess = applyRuntimeOnSuccess;
  maintenanceLease.id = networkCommand.requestId;
  if (maintenanceLease.id == 0) {
    maintenanceLease.id = nextInternalRequestId++;
  }
  maintenanceLease.startedAtMs = millis();
  maintenanceLease.command = networkCommand;
  maintenanceLease.command.requestId = maintenanceLease.id;
  maintenanceLease.command.maintenanceLeaseId = maintenanceLease.id;
  addDebugEvent(DebugCategory::SECURITY, DebugCode::MAINTENANCE_RESERVED,
                static_cast<int32_t>(maintenanceLease.id),
                static_cast<int32_t>(networkCommand.type));
  return true;
}

bool webCommandAllowsUnsafeConfiguration(const WebCommand &command) {
  return controlAllowsConfigurationNow() || command.unsafeWebUiOverride;
}

bool persistentConfigurationMutationAllowed(const WebCommand &command) {
  return settingsPersistenceAvailable() &&
         webCommandAllowsUnsafeConfiguration(command);
}

void holdOrBeginPlannedRestart(const WebCommand &command) {
  WebCommand restart = command;
  restart.type = WebCommandType::RESTART;
  restart.unsafeWebUiOverride = false;
  if (!controlAllowsConfigurationNow() ||
      session.active || getRelaySafetySnapshot().closed ||
      machineLastIntention().holdActive) {
    pendingPlannedRestart = restart;
    plannedRestartHeld = true;
    return;
  }
  plannedRestartHeld = false;
  pendingPlannedRestart = WebCommand{};
  if (!beginMaintenanceLease(restart, false)) {
    rejectWebCommand(restart);
  }
}

void servicePendingPlannedRestart() {
  if (!plannedRestartHeld || maintenanceLease.active) {
    return;
  }
  if (!controlAllowsConfigurationNow() || session.active ||
      getRelaySafetySnapshot().closed || machineLastIntention().holdActive) {
    return;
  }
  const WebCommand restart = pendingPlannedRestart;
  plannedRestartHeld = false;
  pendingPlannedRestart = WebCommand{};
  if (!beginMaintenanceLease(restart, false)) {
    rejectWebCommand(restart);
  }
}

void serviceMaintenanceLease() {
  if (!maintenanceLease.active || maintenanceLease.forwarded ||
      !machineLastIntention().stablyOff ||
      elapsedMs(maintenanceLease.startedAtMs) <
          MAINTENANCE_LEASE_SETTLE_MS) {
    return;
  }
  if (forwardAcceptedNetworkCommand(maintenanceLease.command)) {
    maintenanceLease.forwarded = true;
#ifdef SHOT_STOPPER_HOST_TEST
    WebCommand result;
    result.type = WebCommandType::MAINTENANCE_COMPLETE;
    result.requestId = maintenanceLease.command.requestId;
    result.maintenanceLeaseId = maintenanceLease.id;
    result.config = maintenanceLease.command.config;
    result.succeeded = true;
    const WebCommandType forwardedType = maintenanceLease.command.type;
    if (forwardedType == WebCommandType::SAVE_NETWORK ||
        forwardedType == WebCommandType::FORGET_NETWORK ||
        forwardedType == WebCommandType::CHANGE_DEVICE_PASSWORD ||
        forwardedType == WebCommandType::RESET_DEVICE_PASSWORD ||
        forwardedType == WebCommandType::RESET_NETWORK_AP ||
        forwardedType == WebCommandType::RESTART) {
      hostLastFlushedRuntime = runtimeConfig;
      hostLastFlushedPresets = presetBank;
      hostLastFlushIncludedLive = true;
      ++hostRuntimePersistAttempts;
      if (!hostRuntimePersistSucceeds) {
        result.succeeded = forwardedType == WebCommandType::RESTART;
        runtimePersistFailed = true;
        runtimePersistPending = true;
        runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_RETRY_MS;
      } else {
        runtimePersistPending = false;
        runtimePersistReasonBits = 0;
        runtimePersistFailed = false;
      }
    }
    hostLastMaintenanceSucceeded = result.succeeded;
    completeMaintenanceLease(result);
#endif
  }
}

void queueRuntimePersist(int32_t reasonBits) {
  runtimePersistPending = true;
  runtimePersistReasonBits |= reasonBits;
  runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_DEBOUNCE_MS;
}

void commitLiveRuntimeConfig(const RuntimeConfig &composed, int32_t reasonBits) {
  const bool alertsWereEnabled = soundAlertsEnabled();
  const uint8_t previousNoScaleMode = runtimeConfig.noScaleBbwMode;
  const uint8_t previousScaleMacCacheMode = runtimeConfig.scaleMacCacheMode;
  runtimeConfig = composed;
  publishScaleWorkerPolicy(runtimeConfig, firmwareInitializationComplete);
  if (runtimeConfig.scaleMacCacheMode != previousScaleMacCacheMode) {
    requestScalePreferenceModeReset();
  }
  if (runtimeConfig.noScaleBbwMode != previousNoScaleMode) {
    noScaleShotGuardActivityAtMs = 0;
    noScaleShotGuardArmed = noScaleBbwEnabled(runtimeConfig.noScaleBbwMode);
    resetNoScaleRequireBypassGesture();
    if (!noScaleBbwEnabled(runtimeConfig.noScaleBbwMode)) {
      // Changing settings must not turn an already-held activator into a new
      // relay close. The hold clears on release in serviceNoScaleShotGuard().
      noScaleShotGuardHold = machineLastIntention().holdActive;
    }
  }
  if (!soundAlertsEnabled()) {
    cancelOperationalAlerts();
  }
  publishLogLevels(serialLogLevelFromRuntime(runtimeConfig),
                   static_cast<LogLevel>(runtimeConfig.ringRetainLogLevel));
  configureEspLogRuntime();
  addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED,
                static_cast<int32_t>(runtimeConfig.revision));
  requestBookooSilenceIfConfigured();
  if (!alertsWereEnabled && soundAlertsEnabled()) {
    requestBookooAlertVolumeRestore();
  }
  queueRuntimePersist(reasonBits);
#ifndef SHOT_STOPPER_HOST_TEST
  networkManager.syncLiveRuntime(runtimeConfig, &presetBank);
#endif
  publishRecipeState();
}

#ifdef SHOT_STOPPER_HOST_TEST
void settingsPersistTask(void *) {}
#else
void settingsPersistTask(void *parameter) {
  (void)parameter;
  if (!subscribeCurrentTaskToWatchdog()) {
    reportTaskWatchdogFault();
  }
  for (;;) {
    // Must not block forever: this task is subscribed to the TWDT.
    if (xQueueReceive(settingsPersistQueue, &settingsPersistReceive,
                      pdMS_TO_TICKS(SETTINGS_PERSIST_IDLE_WAIT_MS)) !=
        pdTRUE) {
      (void)persistResetUptimeCheckpoint(millis());
      feedOrTripCurrentTaskWatchdog();
      continue;
    }
    yieldSettingsNvs();
    feedOrTripCurrentTaskWatchdog();
    const bool ok = savePersistedSettings(settingsPersistReceive.blob);
    feedOrTripCurrentTaskWatchdog();
    settingsPersistMux.lock();
    settingsPersistResultReady = true;
    settingsPersistResultOk = ok;
    settingsPersistResultRuntimeRevision =
        settingsPersistReceive.runtimeRevision;
    settingsPersistResultStorageRevision =
        ok ? settingsPersistReceive.blob.storageRevision : 0;
    settingsPersistMux.unlock();
  }
}
#endif

bool settingsPersistenceAvailable() {
  return settingsPersistenceReady && settingsPersistQueue != nullptr &&
         settingsPersistTaskHandle != nullptr;
}

bool initializeSettingsPersistenceWorker() {
  if (settingsPersistenceAvailable()) {
    return true;
  }
  settingsPersistenceReady = false;
  if (settingsPersistTaskHandle != nullptr) {
    return false;
  }
  if (settingsPersistQueue != nullptr) {
    vQueueDelete(settingsPersistQueue);
    settingsPersistQueue = nullptr;
#ifdef SHOT_STOPPER_HOST_TEST
    ++hostSettingsPersistRollbackDeletes;
#endif
  }

#ifdef SHOT_STOPPER_HOST_TEST
  QueueHandle_t queue = hostSettingsPersistQueueCreateSucceeds
                            ? xQueueCreate(1, sizeof(SettingsPersistRequest))
                            : nullptr;
#else
  QueueHandle_t queue = xQueueCreate(1, sizeof(SettingsPersistRequest));
#endif
  if (queue == nullptr) {
    return false;
  }

  // Publish the queue before task creation: a newly scheduled worker may run
  // immediately and must never observe a null receive queue.
  settingsPersistQueue = queue;
  TaskHandle_t task = nullptr;
#ifdef SHOT_STOPPER_HOST_TEST
  const BaseType_t created =
      hostSettingsPersistTaskCreateSucceeds
          ? xTaskCreatePinnedToCore(
                settingsPersistTask, "settings_persist",
                SETTINGS_PERSIST_TASK_STACK_SIZE, nullptr,
                tskIDLE_PRIORITY + 1,
                &task, CONTROL_TASK_CORE)
          : pdFALSE;
#else
  const BaseType_t created = xTaskCreatePinnedToCore(
      settingsPersistTask, "settings_persist",
      SETTINGS_PERSIST_TASK_STACK_SIZE, nullptr, tskIDLE_PRIORITY + 1, &task,
      CONTROL_TASK_CORE);
#endif
  if (created != pdPASS || task == nullptr) {
    // Task creation failed, so no consumer can still reference the queue.
    settingsPersistQueue = nullptr;
    vQueueDelete(queue);
#ifdef SHOT_STOPPER_HOST_TEST
    ++hostSettingsPersistRollbackDeletes;
#endif
    return false;
  }

  settingsPersistTaskHandle = task;
  settingsPersistenceReady = true;
  return true;
}

#ifdef SHOT_STOPPER_HOST_TEST
void releaseSettingsPersistenceWorkerForHost() {
  if (settingsPersistQueue != nullptr) {
    vQueueDelete(settingsPersistQueue);
  }
  settingsPersistQueue = nullptr;
  settingsPersistTaskHandle = nullptr;
  settingsPersistenceReady = false;
}
#endif

#ifndef SHOT_STOPPER_HOST_TEST

void serviceSettingsPersistResult() {
  bool ready = false;
  bool ok = false;
  uint32_t runtimeRevision = 0;
  uint32_t storageRevision = 0;
  settingsPersistMux.lock();
  ready = settingsPersistResultReady;
  if (ready) {
    settingsPersistResultReady = false;
    ok = settingsPersistResultOk;
    runtimeRevision = settingsPersistResultRuntimeRevision;
    storageRevision = settingsPersistResultStorageRevision;
    settingsPersistInFlight = false;
  }
  settingsPersistMux.unlock();
  if (!ready) {
    return;
  }
  if (ok) {
    persistedSettings.storageRevision = storageRevision;
    networkManager.syncDurableStorageRevision(storageRevision);
    runtimePersistFailed = false;
    addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
                  static_cast<int32_t>(runtimeRevision));
    char liveMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
    char liveName[PREFERRED_SCALE_NAME_CAPACITY] = {};
    ScaleHistoryEntry liveHistory[SCALE_HISTORY_CAPACITY] = {};
    copyPreferredScaleMac(liveMac, sizeof(liveMac));
    copyPreferredScaleName(liveName, sizeof(liveName));
    copyScaleHistory(liveHistory);
    if (strncmp(persistedSettings.preferredScaleMac, liveMac,
                PREFERRED_SCALE_MAC_CAPACITY) == 0 &&
        strncmp(persistedSettings.preferredScaleName, liveName,
                PREFERRED_SCALE_NAME_CAPACITY) == 0 &&
        scaleHistoryIdentityEqual(persistedSettings.scaleHistory,
                                  liveHistory)) {
      scaleWorkerClearPreferredDirty();
    } else {
      const ScaleLinkSnapshot link = getScaleLinkSnapshot();
      if (scaleMacNvsWriteAllowed(link.connecting,
                                  link.state == ScaleLinkState::CONNECTED)) {
        runtimePersistPending = true;
        runtimePersistReasonBits |= RUNTIME_PERSIST_REASON_SCALE_MAC;
      }
    }
    if (runtimeConfig.revision != runtimeRevision) {
      runtimePersistPending = true;
      runtimePersistRetryAtMs = millis();
    } else if (!runtimePersistPending) {
      runtimePersistReasonBits = 0;
    }
  } else {
    if (!runtimePersistFailed) {
      addDebugEvent(DebugCategory::CONFIG, DebugCode::RUNTIME_PERSIST_FAILED,
                    static_cast<int32_t>(runtimeRevision),
                    runtimePersistReasonBits);
    }
    runtimePersistFailed = true;
    runtimePersistPending = true;
    runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_RETRY_MS;
  }
}

bool dispatchSettingsPersist() {
  if (!settingsPersistenceAvailable() || settingsPersistInFlight) {
    if (!settingsPersistenceAvailable()) {
      runtimePersistFailed = true;
    }
    return false;
  }
  SettingsPersistRequest &request = settingsPersistRequest;
  request.blob = networkManager.settingsCopy();
  request.blob.runtime = runtimeConfig;
  request.blob.bullseyeMelody = bullseyeMelodyConfig;
  request.blob.presets = presetBank;
  copyPreferredScaleMac(request.blob.preferredScaleMac,
                        sizeof(request.blob.preferredScaleMac));
  copyPreferredScaleName(request.blob.preferredScaleName,
                         sizeof(request.blob.preferredScaleName));
  copyScaleHistory(request.blob.scaleHistory);
  memcpy(persistedSettings.preferredScaleMac, request.blob.preferredScaleMac,
         sizeof(persistedSettings.preferredScaleMac));
  memcpy(persistedSettings.preferredScaleName, request.blob.preferredScaleName,
         sizeof(persistedSettings.preferredScaleName));
  memcpy(persistedSettings.scaleHistory, request.blob.scaleHistory,
         sizeof(persistedSettings.scaleHistory));
  request.runtimeRevision = runtimeConfig.revision;
  if (xQueueSend(settingsPersistQueue, &request, 0) != pdTRUE) {
    return false;
  }
  settingsPersistMux.lock();
  settingsPersistInFlight = true;
  settingsPersistMux.unlock();
  runtimePersistPending = false;
  return true;
}
#endif

void serviceShotStorePersistence() {
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  if (!durableFlashWriteAllowed(session.active || machineIsRunning(),
                                link.connecting)) {
    return;
  }
  if (static_cast<int32_t>(millis() - shotStorePersistRetryAtMs) < 0) {
    return;
  }
  // Do not wait on the durable flash mutex from the 1 ms control loop: a
  // 50 ms take while settings_persist holds the lock stalls pala/weight and
  // can spam SHOT_LOG_PERSIST_FAILED into the 96-event ring.
  constexpr uint32_t kTryLockMs = 0;
  bool anyFail = false;
  if (shotLog.dirty()) {
    if (shotLog.flush(kTryLockMs)) {
      shotLogPersistFailLatched = false;
    } else {
      anyFail = true;
      if (!shotLogPersistFailLatched) {
        shotLogPersistFailLatched = true;
        addDebugEvent(DebugCategory::CONFIG, DebugCode::SHOT_LOG_PERSIST_FAILED,
                      static_cast<int32_t>(shotLog.count()), 0);
      }
    }
  }
  if (shotCurves.dirty()) {
    if (shotCurves.flush(kTryLockMs)) {
      shotCurvePersistFailLatched = false;
    } else {
      anyFail = true;
      if (!shotCurvePersistFailLatched) {
        shotCurvePersistFailLatched = true;
        addDebugEvent(DebugCategory::CONFIG, DebugCode::SHOT_LOG_PERSIST_FAILED,
                      static_cast<int32_t>(shotCurves.count()), 1);
      }
    }
  }
  if (lastShotNvsDirty) {
    if (lastShotStore.save(kTryLockMs)) {
      lastShotNvsDirty = false;
      lastShotPersistFailLatched = false;
    } else {
      anyFail = true;
      if (!lastShotPersistFailLatched) {
        lastShotPersistFailLatched = true;
        addDebugEvent(DebugCategory::CONFIG, DebugCode::SHOT_LOG_PERSIST_FAILED,
                      0, 2);
      }
    }
  }
  shotStorePersistRetryAtMs =
      anyFail ? millis() + SHOT_STORE_PERSIST_RETRY_MS : 0;
}

void serviceRuntimePersistence() {
#ifndef SHOT_STOPPER_HOST_TEST
  serviceSettingsPersistResult();
  bool inFlight = false;
  settingsPersistMux.lock();
  inFlight = settingsPersistInFlight;
  settingsPersistMux.unlock();
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  if (!runtimePersistPending || inFlight || maintenanceLease.active ||
      static_cast<int32_t>(millis() - runtimePersistRetryAtMs) < 0 ||
      !controlAllowsConfigurationNow() ||
      !durableFlashWriteAllowed(false, link.connecting)) {
    return;
  }
  if (!dispatchSettingsPersist()) {
    runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_RETRY_MS;
  }
#else
  if (!settingsPersistenceAvailable()) {
    if (runtimePersistPending) {
      runtimePersistFailed = true;
    }
    return;
  }
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  if (!runtimePersistPending || maintenanceLease.active ||
      static_cast<int32_t>(millis() - runtimePersistRetryAtMs) < 0 ||
      !controlAllowsConfigurationNow() ||
      !durableFlashWriteAllowed(false, link.connecting)) {
    return;
  }
  ++hostRuntimePersistAttempts;
  if (!hostRuntimePersistSucceeds) {
    if (!runtimePersistFailed) {
      addDebugEvent(DebugCategory::CONFIG, DebugCode::RUNTIME_PERSIST_FAILED,
                    static_cast<int32_t>(runtimeConfig.revision),
                    runtimePersistReasonBits);
    }
    runtimePersistFailed = true;
    runtimePersistPending = true;
    runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_RETRY_MS;
    return;
  }
  hostLastFlushedRuntime = runtimeConfig;
  hostLastFlushedPresets = presetBank;
  hostLastFlushIncludedLive = true;
  runtimePersistPending = false;
  runtimePersistReasonBits = 0;
  runtimePersistFailed = false;
#endif
}

bool persistBleCompanionEnabled(bool enabled);
bool persistBleScanIntensity(BleScanIntensity intensity);

void completeMaintenanceLease(const WebCommand &result) {
  if (!maintenanceLease.active ||
      result.maintenanceLeaseId != maintenanceLease.id) {
    addDebugEvent(DebugCategory::WEB, DebugCode::WEB_COMMAND_REJECTED,
                  static_cast<int32_t>(result.type));
    return;
  }
  if (result.succeeded && maintenanceLease.applyRuntimeOnSuccess) {
    // Live runtime is apply-first. Never copy persist results over RAM.
    addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_ACCEPTED,
                  static_cast<int32_t>(runtimeConfig.revision));
    if (maintenanceLease.command.type == WebCommandType::PERSIST_RUNTIME) {
      runtimePersistReasonBits = 0;
    }
    requestBookooSilenceIfConfigured();
  }
  if (!result.succeeded &&
      maintenanceLease.command.type == WebCommandType::PERSIST_RUNTIME &&
      maintenanceLease.id == runtimePersistRequestId) {
    if (!runtimePersistFailed) {
      addDebugEvent(
          DebugCategory::CONFIG, DebugCode::RUNTIME_PERSIST_FAILED,
          static_cast<int32_t>(maintenanceLease.command.config.revision),
          runtimePersistReasonBits);
    }
    runtimePersistFailed = true;
    // RUNTIME_PERSIST_FAILED already formats a detailed Serial line.
    runtimePersistCandidate = maintenanceLease.command.config;
    runtimePersistPending = true;
    runtimePersistRetryAtMs = millis() + RUNTIME_PERSIST_RETRY_MS;
  }
  addDebugEvent(DebugCategory::SECURITY,
                result.succeeded ? DebugCode::MAINTENANCE_COMPLETED
                                 : DebugCode::COMMAND_FAILED,
                static_cast<int32_t>(maintenanceLease.id));
  maintenanceLease = MaintenanceLease{};
  if (machineLastIntention().holdActive) {
    transitionTo(StopperState::REQUIRES_OFF);
  }
}
