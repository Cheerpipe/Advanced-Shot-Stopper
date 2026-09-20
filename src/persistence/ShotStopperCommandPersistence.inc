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

uint32_t nextPersistenceIoRetry(uint32_t currentMs, uint32_t baseMs) {
  if (currentMs == 0) return baseMs;
  return currentMs >= PERSIST_IO_RETRY_MAX_MS / 2U
             ? PERSIST_IO_RETRY_MAX_MS
             : currentMs * 2U;
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

void holdOrBeginResetHistoryClear(const WebCommand &command) {
  if (resetHistoryClearHeld) {
    rejectWebCommand(command);
    return;
  }
  WebCommand clear = command;
  clear.unsafeWebUiOverride = false;
  clear.resetHistoryUnsafeCount = safetyResetStatus.unsafeResetCount;
  if (!controlAllowsConfigurationNow() || session.active ||
      getRelaySafetySnapshot().closed || machineLastIntention().holdActive) {
    pendingResetHistoryClear = clear;
    resetHistoryClearHeld = true;
    return;
  }
  if (!beginMaintenanceLease(clear, false)) rejectWebCommand(clear);
}

void servicePendingResetHistoryClear() {
  if (!resetHistoryClearHeld || !controlAllowsConfigurationNow() ||
      session.active || getRelaySafetySnapshot().closed ||
      machineLastIntention().holdActive) {
    return;
  }
  const WebCommand clear = pendingResetHistoryClear;
  if (!beginMaintenanceLease(clear, false)) return;
  resetHistoryClearHeld = false;
  pendingResetHistoryClear = WebCommand{};
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
        runtimePersistIoRetryMs = nextPersistenceIoRetry(
            runtimePersistIoRetryMs, RUNTIME_PERSIST_RETRY_MS);
        runtimePersistRetryAtMs = millis() + runtimePersistIoRetryMs;
      } else {
        runtimePersistPending = false;
        runtimePersistReasonBits = 0;
        runtimePersistFailed = false;
        runtimePersistIoRetryMs = 0;
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
  runtimePersistIoRetryMs = 0;
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
  if (noScaleBbwModeValue(runtimeConfig.noScaleBbwMode) !=
      noScaleBbwModeValue(previousNoScaleMode)) {
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

bool serviceBleScanPersist();

#ifndef SHOT_STOPPER_HOST_TEST
bool shotStoreWorkerWriteAllowed() {
  ControlGateSnapshot control;
  copyControlGate(control);
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  return !control.maintenanceLeaseActive &&
         durableFlashWriteAllowed(control.activeCycle || control.relayClosed ||
                                      control.machineRunning ||
                                      control.physicalActivatorOn,
                                  link.connecting);
}

void waitForShotStoreWriteGate() {
  while (!shotStoreWorkerWriteAllowed()) {
    feedOrTripCurrentTaskWatchdog();
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}
#endif

void persistShotStoreImage() {
  const uint32_t lockTimeoutsBefore = flashIoLockTimeouts();
  ActivationStoresFlushReport report;
  do {
#ifndef SHOT_STOPPER_HOST_TEST
    waitForShotStoreWriteGate();
#endif
    report = shotStorePersistImage->serviceStep(FLASH_IO_LOCK_TIMEOUT_MS,
                                                addDebugEvent);
    if (!report.complete && !report.anyFail) yieldFlashIo();
  } while (!report.complete && !report.anyFail);
  bool ok = !report.anyFail;
  if (shotStorePersistImageLastShotDirty) {
#ifndef SHOT_STOPPER_HOST_TEST
    waitForShotStoreWriteGate();
    yieldFlashIo();
#endif
    if (!lastShotPersistImage->save(FLASH_IO_LOCK_TIMEOUT_MS)) ok = false;
  }
  shotStorePersistMux.lock();
  shotStorePersistResultReady = true;
  shotStorePersistResultOk = ok;
  shotStorePersistResultIoFail =
      !ok && flashIoLockTimeouts() == lockTimeoutsBefore;
  shotStorePersistResultGeneration = shotStorePersistImageGeneration;
  shotStorePersistInFlight = false;
  shotStorePersistMux.unlock();
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
    PersistenceWork work = PersistenceWork::SETTINGS;
    if (xQueueReceive(settingsPersistQueue, &work,
                      pdMS_TO_TICKS(SETTINGS_PERSIST_IDLE_WAIT_MS)) !=
        pdTRUE) {
      feedOrTripCurrentTaskWatchdog();
      ControlGateSnapshot control;
      copyControlGate(control);
      const ScaleLinkSnapshot link = getScaleLinkSnapshot();
      if (!control.maintenanceLeaseActive &&
          durableFlashWriteAllowed(control.activeCycle || control.relayClosed ||
                                       control.machineRunning ||
                                       control.physicalActivatorOn,
                                   link.connecting)) {
        (void)persistResetUptimeCheckpoint(millis());
        serviceBleScanPersist();
      }
      continue;
    }
    yieldSettingsNvs();
    feedOrTripCurrentTaskWatchdog();
    if (work == PersistenceWork::SHOT_STORES) {
      persistShotStoreImage();
      feedOrTripCurrentTaskWatchdog();
      continue;
    }
    // The producer owns the mailbox until enqueue; this worker owns it until
    // publishing the result. Only the control task consumes that result.
    const uint32_t lockTimeoutsBefore = flashIoLockTimeouts();
    const bool ok = savePersistedSettings(settingsPersistRequest.blob);
    const bool lockContended =
        !ok && flashIoLockTimeouts() != lockTimeoutsBefore;
    feedOrTripCurrentTaskWatchdog();
    settingsPersistMux.lock();
    settingsPersistResultReady = true;
    settingsPersistResultOk = ok;
    settingsPersistResultLockContended = lockContended;
    settingsPersistResultRuntimeRevision =
        settingsPersistRequest.runtimeRevision;
    settingsPersistResultStorageRevision =
        ok ? settingsPersistRequest.blob.storageRevision : 0;
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
  if (shotStorePersistImage == nullptr) {
    void *block = allocExternal(sizeof(ActivationStores));
    if (block == nullptr) return false;
    shotStorePersistImage = new (block) ActivationStores();
  }
  if (lastShotPersistImage == nullptr) {
    void *block = allocExternal(sizeof(LastShotStore));
    if (block == nullptr) return false;
    lastShotPersistImage = new (block) LastShotStore();
  }

#ifdef SHOT_STOPPER_HOST_TEST
  QueueHandle_t queue = hostSettingsPersistQueueCreateSucceeds
                            ? xQueueCreate(PERSISTENCE_WORK_QUEUE_DEPTH,
                                           sizeof(PersistenceWork))
                            : nullptr;
#else
  QueueHandle_t queue =
      xQueueCreate(PERSISTENCE_WORK_QUEUE_DEPTH, sizeof(PersistenceWork));
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
                tskIDLE_PRIORITY, &task, PERSISTENCE_TASK_CORE)
          : pdFALSE;
#else
  const BaseType_t created = xTaskCreatePinnedToCore(
      settingsPersistTask, "settings_persist",
      SETTINGS_PERSIST_TASK_STACK_SIZE, nullptr, tskIDLE_PRIORITY, &task,
      PERSISTENCE_TASK_CORE);
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
  if (shotStorePersistImage != nullptr) {
    shotStorePersistImage->~ActivationStores();
    free(shotStorePersistImage);
    shotStorePersistImage = nullptr;
  }
  if (lastShotPersistImage != nullptr) {
    lastShotPersistImage->~LastShotStore();
    free(lastShotPersistImage);
    lastShotPersistImage = nullptr;
  }
}
#endif

#ifndef SHOT_STOPPER_HOST_TEST

void serviceSettingsPersistResult() {
  bool ready = false;
  bool ok = false;
  bool lockContended = false;
  uint32_t runtimeRevision = 0;
  uint32_t storageRevision = 0;
  settingsPersistMux.lock();
  ready = settingsPersistResultReady;
  if (ready) {
    settingsPersistResultReady = false;
    ok = settingsPersistResultOk;
    lockContended = settingsPersistResultLockContended;
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
    runtimePersistIoRetryMs = 0;
    addDebugEvent(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
                  static_cast<int32_t>(runtimeRevision));
    if (pendingPresetPersistence.requestId != 0 &&
        static_cast<int32_t>(runtimeRevision -
                             pendingPresetPersistence.revision) >= 0) {
      const bool notifyPresets = pendingPresetPersistence.notifyPresets;
      const bool applyMachineTemperature =
          pendingPresetPersistence.applyMachineTemperature;
      WebCommand persisted;
      persisted.requestId = pendingPresetPersistence.requestId;
      reportControlCommandResult(persisted, CommandResultState::PERSISTED);
      pendingPresetPersistence = {};
      if (applyMachineTemperature) requestActivePresetMachineTemperature();
      if (notifyPresets) queueWebhookPresetsChanged();
    }
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
        runtimePersistIoRetryMs = 0;
        runtimePersistReasonBits |= RUNTIME_PERSIST_REASON_SCALE_MAC;
      }
    }
    if (runtimeConfig.revision != runtimeRevision) {
      runtimePersistPending = true;
      runtimePersistIoRetryMs = 0;
      runtimePersistRetryAtMs = millis();
    } else if (!runtimePersistPending) {
      runtimePersistReasonBits = 0;
    }
    if (runtimeConfig.revision == runtimeRevision &&
        quickSettingsPersistPending) {
      quickSettingsPersistPending = false;
      queueWebhookQuickSettingsChanged();
    }
  } else {
    if (!runtimePersistFailed) {
      addDebugEvent(DebugCategory::CONFIG, DebugCode::RUNTIME_PERSIST_FAILED,
                    static_cast<int32_t>(runtimeRevision),
                    runtimePersistReasonBits);
    }
    runtimePersistFailed = true;
    runtimePersistPending = true;
    if (!lockContended) {
      runtimePersistIoRetryMs = nextPersistenceIoRetry(
          runtimePersistIoRetryMs, RUNTIME_PERSIST_RETRY_MS);
    }
    runtimePersistRetryAtMs =
        millis() + (lockContended ? RUNTIME_PERSIST_RETRY_MS
                                  : runtimePersistIoRetryMs);
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
  networkManager.copySettings(request.blob);
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
  // Publish ownership before waking a worker that may finish immediately.
  settingsPersistMux.lock();
  settingsPersistInFlight = true;
  settingsPersistMux.unlock();
  const PersistenceWork work = PersistenceWork::SETTINGS;
  if (xQueueSend(settingsPersistQueue, &work, 0) != pdTRUE) {
    settingsPersistMux.lock();
    settingsPersistInFlight = false;
    settingsPersistMux.unlock();
    return false;
  }
  runtimePersistPending = false;
  return true;
}
#endif

// Durable flush of the staged BLE scan settings. Runs on the settings_persist
// worker (never the Arduino control loop); the worker's 1 s idle cadence is
// the retry backoff on failure.
bool serviceBleScanPersist() {
#if defined(SHOT_STOPPER_HOST_TEST)
  // Host harness: no NVS layer; a staged change completes immediately.
  bleScanPersistMux.lock();
  const bool pending = bleScanPersistPending || bleScanBackoffPersistPending ||
                       bleScanBoostPersistPending;
  bleScanPersistPending = false;
  bleScanBackoffPersistPending = false;
  bleScanBoostPersistPending = false;
  bleScanPersistResultOk = true;
  bleScanPersistResultReady = pending;
  bleScanPersistMux.unlock();
  return true;
#else
  bleScanPersistMux.lock();
  const bool pending = bleScanPersistPending;
  const bool backoffPending = bleScanBackoffPersistPending;
  const bool boostPending = bleScanBoostPersistPending;
  // An unstaged field keeps its durable value, so all three share one save.
  const uint8_t intensity = pending ? bleScanPersistIntensity
                                    : bleScanPersistedSettings.scanIntensity;
  const uint8_t backoffMin = backoffPending
                                 ? bleScanPersistBackoffMin
                                 : bleScanPersistedSettings.scanBackoffMin;
  const uint8_t boostMin = boostPending
                               ? bleScanPersistBoostMin
                               : bleScanPersistedSettings.scanBoostMin;
  bleScanPersistMux.unlock();
  if (!pending && !backoffPending && !boostPending) {
    return true;
  }
  const bool ok = persistBleScanSettings(bleScanPersistedSettings, intensity,
                                         backoffMin, boostMin);
  bleScanPersistMux.lock();
  if (ok && pending && bleScanPersistIntensity == intensity) {
    // A newer staged value stays pending for the next idle pass.
    bleScanPersistPending = false;
  }
  if (ok && backoffPending && bleScanPersistBackoffMin == backoffMin) {
    bleScanBackoffPersistPending = false;
  }
  if (ok && boostPending && bleScanPersistBoostMin == boostMin) {
    bleScanBoostPersistPending = false;
  }
  bleScanPersistResultOk = ok;
  bleScanPersistResultReady = true;
  bleScanPersistMux.unlock();
  return ok;
#endif
}

// Control-loop side: consume the worker's flush result, report the held
// BLE scan command as PERSISTED, and surface failures once per latch.
void serviceBleScanPersistResult() {
  bool ready = false;
  bool ok = false;
  bleScanPersistMux.lock();
  ready = bleScanPersistResultReady;
  if (ready) {
    bleScanPersistResultReady = false;
    ok = bleScanPersistResultOk;
  }
  bleScanPersistMux.unlock();
  if (!ready) {
    return;
  }
  if (ok) {
    bleScanPersistFailLatched = false;
    // The combined dual-slot save covers every id accepted since the last
    // flush; report each one, oldest first, outside the staging mutex.
    uint32_t reportedIds[PENDING_BLE_SCAN_REQUEST_CAPACITY];
    uint8_t reportedCount = 0;
    bleScanPersistMux.lock();
    reportedCount = pendingBleScanRequestIdCount;
    for (uint8_t i = 0; i < reportedCount; ++i) {
      reportedIds[i] = pendingBleScanRequestIds[i];
    }
    pendingBleScanRequestIdCount = 0;
    bleScanPersistMux.unlock();
    for (uint8_t i = 0; i < reportedCount; ++i) {
      WebCommand persisted;
      persisted.requestId = reportedIds[i];
      reportControlCommandResult(persisted, CommandResultState::PERSISTED);
    }
  } else if (!bleScanPersistFailLatched) {
    bleScanPersistFailLatched = true;
    addDebugEvent(DebugCategory::CONFIG, DebugCode::RUNTIME_PERSIST_FAILED,
                  0, RUNTIME_PERSIST_REASON_BLE_SCAN);
  }
}

void serviceShotStorePersistence() {
  bool resultReady = false;
  bool resultOk = false;
  bool resultIoFail = false;
  uint32_t resultGeneration = 0;
  shotStorePersistMux.lock();
  resultReady = shotStorePersistResultReady;
  if (resultReady) {
    shotStorePersistResultReady = false;
    resultOk = shotStorePersistResultOk;
    resultIoFail = shotStorePersistResultIoFail;
    resultGeneration = shotStorePersistResultGeneration;
  }
  const bool inFlight = shotStorePersistInFlight;
  shotStorePersistMux.unlock();
  if (resultReady) {
    const uint32_t liveGeneration =
        shotStoreDirtyGeneration.load(std::memory_order_acquire);
    const bool current = liveGeneration == resultGeneration;
    {
      TaskLockGuard lock(shotStoreMutex);
      activationStores.acknowledgePersisted(*shotStorePersistImage,
                                            resultOk && current);
      if (resultOk && current && shotStorePersistImageLastShotDirty) {
        lastShotNvsDirty = false;
        lastShotPersistFailLatched = false;
      }
    }
    if (resultOk) {
      shotStorePersistIoRetryMs = 0;
      shotStorePersistRetryAtMs = 0;
      if (controllerStartedPending && !shotLog.dirty()) {
        (void)enqueueControllerStartedWebhook();
      }
    } else {
      if (resultIoFail) {
        shotStorePersistIoRetryMs = nextPersistenceIoRetry(
            shotStorePersistIoRetryMs, SHOT_STORE_PERSIST_RETRY_MS);
      }
      shotStorePersistRetryAtMs =
          millis() + (resultIoFail ? shotStorePersistIoRetryMs
                                  : SHOT_STORE_PERSIST_RETRY_MS);
    }
  }
  if (inFlight || !settingsPersistenceAvailable()) return;
  if (maintenanceLease.active) return;
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  if (!durableFlashWriteAllowed(session.active || machineIsRunning(),
                                link.connecting)) {
    return;
  }
  const uint32_t dirtyGeneration =
      shotStoreDirtyGeneration.load(std::memory_order_relaxed);
  if (dirtyGeneration != shotStoreObservedDirtyGeneration) {
    shotStoreObservedDirtyGeneration = dirtyGeneration;
    shotStorePersistIoRetryMs = 0;
    shotStorePersistRetryAtMs = 0;
  }
  if (static_cast<int32_t>(millis() - shotStorePersistRetryAtMs) < 0) {
    return;
  }
  {
    TaskLockGuard lock(shotStoreMutex);
    if (!shotLog.dirty() && !shotCurves.dirty() && !historyLog.dirty() &&
        !lastShotNvsDirty) {
      return;
    }
    *shotStorePersistImage = activationStores;
    *lastShotPersistImage = lastShotStore;
    shotStorePersistImageLastShotDirty = lastShotNvsDirty;
    shotStorePersistImageGeneration = dirtyGeneration;
  }
  shotStorePersistMux.lock();
  shotStorePersistInFlight = true;
  shotStorePersistMux.unlock();
  const PersistenceWork work = PersistenceWork::SHOT_STORES;
  if (xQueueSend(settingsPersistQueue, &work, 0) != pdTRUE) {
    shotStorePersistMux.lock();
    shotStorePersistInFlight = false;
    shotStorePersistMux.unlock();
    return;
  }
#ifdef SHOT_STOPPER_HOST_TEST
  PersistenceWork queued = PersistenceWork::SETTINGS;
  if (xQueueReceive(settingsPersistQueue, &queued, 0) == pdTRUE &&
      queued == PersistenceWork::SHOT_STORES) {
    persistShotStoreImage();
    serviceShotStorePersistence();
  }
#endif
}

void serviceRuntimePersistence(
    const ScaleLinkSnapshot &link = getScaleLinkSnapshot()) {
#ifndef SHOT_STOPPER_HOST_TEST
  serviceSettingsPersistResult();
  serviceBleScanPersistResult();
  bool inFlight = false;
  settingsPersistMux.lock();
  inFlight = settingsPersistInFlight;
  settingsPersistMux.unlock();
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
  serviceBleScanPersist();  // host: staged flush completes immediately
  serviceBleScanPersistResult();
  if (!settingsPersistenceAvailable()) {
    if (runtimePersistPending) {
      runtimePersistFailed = true;
    }
    return;
  }
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
    runtimePersistIoRetryMs = nextPersistenceIoRetry(
        runtimePersistIoRetryMs, RUNTIME_PERSIST_RETRY_MS);
    runtimePersistRetryAtMs = millis() + runtimePersistIoRetryMs;
    return;
  }
  hostLastFlushedRuntime = runtimeConfig;
  hostLastFlushedPresets = presetBank;
  hostLastFlushIncludedLive = true;
  runtimePersistPending = false;
  runtimePersistReasonBits = 0;
  runtimePersistFailed = false;
  runtimePersistIoRetryMs = 0;
  if (pendingPresetPersistence.requestId != 0 &&
      static_cast<int32_t>(runtimeConfig.revision -
                           pendingPresetPersistence.revision) >= 0) {
    const bool notifyPresets = pendingPresetPersistence.notifyPresets;
    const bool applyMachineTemperature =
        pendingPresetPersistence.applyMachineTemperature;
    pendingPresetPersistence = {};
    if (applyMachineTemperature) requestActivePresetMachineTemperature();
    if (notifyPresets) queueWebhookPresetsChanged();
  }
  if (quickSettingsPersistPending) {
    quickSettingsPersistPending = false;
    queueWebhookQuickSettingsChanged();
  }
#endif
}

void persistBleScanIntensity(BleScanIntensity intensity);

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
  if (result.succeeded &&
      maintenanceLease.command.type == WebCommandType::CLEAR_RESET_HISTORY) {
    safetyResetStatus.resetHistoryCount = 0;
    for (ResetHistoryEntry &entry : safetyResetStatus.resetHistory)
      entry = ResetHistoryEntry{};
  }
  if (!result.succeeded &&
      maintenanceLease.command.type == WebCommandType::PERSIST_RUNTIME &&
      maintenanceLease.id == pendingPresetPersistence.requestId) {
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
    runtimePersistIoRetryMs = nextPersistenceIoRetry(
        runtimePersistIoRetryMs, RUNTIME_PERSIST_RETRY_MS);
    runtimePersistRetryAtMs = millis() + runtimePersistIoRetryMs;
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
