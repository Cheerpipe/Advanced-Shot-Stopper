// Arduino entry points
// ---------------------------------------------------------------------------

bool usbConsoleJumperPresent() {
  pinMode(USB_CONSOLE_GPIO, INPUT_PULLUP);
  return digitalRead(USB_CONSOLE_GPIO) == USB_CONSOLE_ACTIVE_LEVEL;
}

// cppcheck-suppress unusedFunction ; Arduino framework entry point
void setup() {
  bootStartedAtMs = millis();
  bootCapabilities = BootCapabilities{};
  bootState = BootState::BOOTING;
  bootDegraded = false;
  firmwareInitializationComplete = false;
#ifdef SHOT_STOPPER_HOST_TEST
  // Host tests may invoke setup() after constructing a ready harness. Its task
  // stub never runs, so tear down that synthetic worker before boot injection.
  releaseSettingsPersistenceWorkerForHost();
  delete relaySafetyTimer;
  delete operationalLimitTimer;
  relaySafetyTimer = nullptr;
  operationalLimitTimer = nullptr;
  independentSafetyTimer.resetForHost();
#endif
  // Safe OPEN before Serial, EEPROM or BLE. Arduino-ESP32 3.x rejects
  // digitalWrite until the pad is a GPIO. After reset the pin is Hi-Z and
  // the output latch is 0, which is OPEN for the active-HIGH relay.
  machineInitialize();
  initializeScaleConnectedLed();
#ifndef SHOT_STOPPER_HOST_TEST
  set_arduino_panic_handler(shotStopperPanicHandler, nullptr);
#endif
  safetyResetStatus = beginSafetyResetGuard();

  localBuzzer.begin(BUZZER_GPIO);

  platformClockReady = setCpuFrequencyMhz(80);
  relaySafetyTimersReady = initializeRelaySafetyTimer();
  taskWatchdogReady =
      configureTaskWatchdog() && subscribeCurrentTaskToWatchdog();
  initializeRelaySafetyStateAfterBoot();

  if (EXTERNAL_SAFETY_HARDWARE_PRESENT && readCircuitFeedbackClosed()) {
    tripRelaySafety(RelaySafetyFault::FEEDBACK_STUCK_CLOSED);
  }

#ifndef SHOT_STOPPER_HOST_TEST
  // A pending destructive operation always resumes before normal storage,
  // BLE, Wi-Fi or HTTP startup. A new gesture is accepted only on cold power.
  resumePendingBootRecovery();
  maybeRunBootRecoveryGesture();
  machineOnActivatorReady();
#endif

  // Default builds keep USB Serial/JTAG off until GPIO4 is jumpered to GND.
  // Serial is HWCDC (native USB), not UART0 — see ShotStopperUsbConsole.h.
  // -DSHOT_STOPPER_ENABLE_JTAG=1 starts CDC at boot (OpenOCD + CLI, no jumper).
  // Latch the enable source for Diagnostic; live IO4 is re-read each status.
  if (SHOT_STOPPER_ENABLE_JTAG == 1) {
    usbSerialEnableSource = UsbSerialEnableSource::COMPILE_FLAG;
    Serial.begin(SERIAL_BAUD);
    installEspLogSink();
  } else if (usbConsoleJumperPresent()) {
    usbSerialEnableSource = UsbSerialEnableSource::JUMPER;
    Serial.begin(SERIAL_BAUD);
    installEspLogSink();
  }

  persistenceReady = EEPROM.begin(EEPROM_SIZE);
  bool flashIoReady = true;
  bool settingsLoaded = persistenceReady;
  bool settingsDurable = persistenceReady;
#ifndef SHOT_STOPPER_HOST_TEST
  flashIoReady = ensureFlashIoMutex();
  if (!flashIoReady) {
    addDebugEvent(DebugCategory::CONFIG, DebugCode::INITIALIZATION_FAILED,
                  BOOT_SUBSYSTEM_PERSISTENCE);
  }
  // RTC captures the prior uptime immediately across warm resets; NVS keeps
  // the history when RTC memory is lost (for example after power loss).
  persistResetHistoryAfterBoot(safetyResetStatus);
  settingsLoaded = false;
  settingsDurable = false;
  if (persistenceReady && loadPersistedSettings(persistedSettings)) {
    settingsLoaded = true;
    settingsDurable = true;
    noteDurableStorageRevision(persistedSettings.storageRevision);
  } else if (persistenceReady) {
    if (initializeDefaultSettings(persistedSettings)) {
      settingsLoaded = true;
      settingsDurable = savePersistedSettings(persistedSettings);
      if (!settingsDurable) {
        addDebugEvent(DebugCategory::CONFIG, DebugCode::INITIALIZATION_FAILED,
                      BOOT_SUBSYSTEM_SETTINGS_SAVE);
      }
    }
  }
  if (!loadBleCompanionSettings(bleCompanionPersistedSettings)) {
    bleCompanionPersistedSettings = BleCompanionPersistedSettings{};
    if (!saveBleCompanionSettings(bleCompanionPersistedSettings)) {
      addDebugEvent(DebugCategory::CONFIG, DebugCode::INITIALIZATION_FAILED,
                    BOOT_SUBSYSTEM_SETTINGS_SAVE);
    }
  }
  const bool bleCompanionConfigured =
      bleCompanionPersistedSettings.enabled != 0;
  applyLiveBleScanIntensity(clampBleScanIntensity(
      bleCompanionPersistedSettings.scanIntensity));
  bleCompanionStatusSnapshot.configuredEnabled = bleCompanionConfigured;
  bleCompanionRuntimeSnapshot.configuredEnabled = bleCompanionConfigured;
  if (bleCompanionConfigured) {
    // The profile object is allocated once per boot. Backend GATT storage is
    // registered only when enabled; reconnects never allocate this object.
    static_assert(sizeof(ShotStopperBleCompanion) >= 64u &&
                      sizeof(ShotStopperBleCompanion) <= 8192u,
                  "ShotStopperBleCompanion size outside expected host budget");
    void *storage = allocExternalOrInternal(sizeof(ShotStopperBleCompanion));
    if (storage != nullptr) {
      bleCompanion = new (storage) ShotStopperBleCompanion();
      bleCompanionRuntimeSnapshot.enabled = true;
    } else {
      bleCompanionStatusSnapshot.lastReject =
          BleCompanionRejectReason::ALLOCATION_FAILED;
      addDebugEvent(DebugCategory::SCALE, DebugCode::INITIALIZATION_FAILED,
                    BOOT_SUBSYSTEM_BLE);
    }
  }
  bleCompanionStatusSnapshot.restartRequired =
      bleCompanionConfigured != bleCompanionRuntimeSnapshot.enabled;
  if (settingsLoaded) {
    runtimeConfig = persistedSettings.runtime;
    bullseyeMelodyConfig = persistedSettings.bullseyeMelody;
    presetBank = persistedSettings.presets;
    ensureShotPresetBank(presetBank, runtimeConfig.retareWindowMs,
                         runtimeConfig.autoRetare);
    scaleWorkerLoadPreferred(persistedSettings.preferredScaleMac,
                             persistedSettings.preferredScaleName,
                             persistedSettings.scaleHistory);
  }
#else
  runtimeConfig = RuntimeConfig{};
  bullseyeMelodyConfig = BullseyeMelodyConfig{};
  seedDefaultShotPresetBank(presetBank);
#endif
  (void)localBuzzer.configureBullseyeRtttl(bullseyeMelodyConfig.rtttl);
  publishLogLevels(serialLogLevelFromRuntime(runtimeConfig),
                   static_cast<LogLevel>(runtimeConfig.ringRetainLogLevel));
  configureEspLogRuntime();
  publishRecipeState();
  publishScaleWorkerPolicy(runtimeConfig, false);

  logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_RESET_REASON,
          static_cast<int32_t>(safetyResetStatus.reasonCode));
  logEmit(relaySafetyTimersReady ? LogLevel::INFO : LogLevel::CRITICAL,
          DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
          BOOT_SUBSYSTEM_RELAY_TIMERS, relaySafetyTimersReady ? 1 : 0);
  logEmit(platformClockReady ? LogLevel::INFO : LogLevel::CRITICAL,
          DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM, BOOT_SUBSYSTEM_CPU,
          platformClockReady ? 1 : 0);
  if (!platformClockReady) {
    addDebugEvent(DebugCategory::SECURITY, DebugCode::INITIALIZATION_FAILED,
                  BOOT_SUBSYSTEM_CPU);
  }
  logEmit(taskWatchdogReady ? LogLevel::INFO : LogLevel::CRITICAL,
          DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
          BOOT_SUBSYSTEM_TASK_WDT, taskWatchdogReady ? 1 : 0);
  if (safetyResetStatus.recoveryRequired) {
    addDebugEvent(DebugCategory::SECURITY, DebugCode::SAFETY_LOCKOUT_ACTIVE);
  }

#ifndef SHOT_STOPPER_HOST_TEST
  shotLog.load();
  shotLog.onBoot();
  shotLog.save();
  shotCurves.load();
  lastShotStore.load();
  persistedLastShot = lastShotStore.get();
  if (persistedLastShot.valid) {
    ShotCurveRecord newest = {};
    if (shotCurves.copyNewestFirst(&newest, 1) == 1) {
      lastShotCurve = newest;
    }
  }
#else
  shotLog.load();
  shotLog.onBoot();
  shotCurves.load();
  lastShotStore.load();
  persistedLastShot = lastShotStore.get();
  if (persistedLastShot.valid) {
    ShotCurveRecord newest = {};
    if (shotCurves.copyNewestFirst(&newest, 1) == 1) {
      lastShotCurve = newest;
    }
  }
#endif

  addDebugEvent(DebugCategory::BOOT, DebugCode::BOOT_BANNER,
                static_cast<int32_t>(shotLog.bootId()));

  if (!persistenceReady) {
    addDebugEvent(DebugCategory::CONFIG, DebugCode::INITIALIZATION_FAILED,
                  BOOT_SUBSYSTEM_PERSISTENCE);
    logEmit(LogLevel::ERROR, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_PERSISTENCE, 0);
  } else {
    logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_PERSISTENCE, 1);
  }

  addDebugEvent(DebugCategory::BOOT, DebugCode::BOOT_RUNTIME_CONFIG,
                weightToCentigrams(
                    static_cast<float>(runtimeConfig.goalWeightG)),
                weightToCentigrams(runtimeConfig.weightOffsetG));

#ifndef SHOT_STOPPER_HOST_TEST
  ScaleWorkerBridgeCallbacks scaleBridge;
  scaleBridge.syncNetworkRf = syncScaleWorkerNetworkRf;
  const bool scaleBridgeOk = configureScaleWorkerBridge(scaleBridge);
#else
  const bool scaleBridgeOk = true;
#endif
  const bool scaleWorkerOk = scaleBridgeOk && initializeScaleWorker();
  if (!scaleWorkerOk) {
    logEmit(LogLevel::ERROR, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_SCALE_WORKER, 0);
  } else {
    logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_SCALE_WORKER, 1);
  }

  webCommandQueue =
      xQueueCreate(WEB_COMMAND_QUEUE_LENGTH, sizeof(WebCommand));
  const bool webQueueOk = webCommandQueue != nullptr;
  if (!webQueueOk) {
    logEmit(LogLevel::ERROR, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_WEB_QUEUE, 0);
  } else {
    logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_WEB_QUEUE, 1);
  }

  const bool settingsPersistenceOk = initializeSettingsPersistenceWorker();
  if (!settingsPersistenceOk) {
    logEmit(LogLevel::WARNING, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_SETTINGS_SAVE, 0);
  } else {
    logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_SETTINGS_SAVE, 1);
  }

  hwmon.begin();
  hwmonSnapshot = hwmon.sample(1);
  publishControlStatus();
  bool networkOk = true;
  bool psramOk = true;
#ifndef SHOT_STOPPER_HOST_TEST
  // Runs before the network task so the OTA slot pair and this boot's
  // pending-verify state are known the first time the Web UI is served.
  shotstopper::ShotStopperOta::instance().begin();
  if (settingsLoaded && webCommandQueue != nullptr) {
    NetworkBridgeCallbacks callbacks;
    callbacks.copyControlStatus = copyControlStatus;
    callbacks.copyControlGate = copyControlGate;
    callbacks.refreshControlStatus = refreshControlStatus;
    callbacks.enqueueWebCommand = enqueueWebCommand;
    callbacks.copyDebugEvents = copyDebugEvents;
    callbacks.addDebugEvent = addDebugEvent;
    callbacks.reportTaskWatchdogFault = reportTaskWatchdogFault;
    callbacks.requestSafeRestart = requestSafeRestart;
    callbacks.copyShotRecords = copyShotRecords;
    callbacks.copyShotCurves = copyShotCurves;
    callbacks.deleteShotRecord = deleteShotRecord;
    callbacks.rateShotRecord = rateShotRecord;
    callbacks.rateLastShot = rateLastShot;
    callbacks.clearShotLog = clearShotLog;
    callbacks.clearLastShot = clearLastShot;
    callbacks.resetAllDurableStores = resetAllDurableStoresForNetwork;
    callbacks.copyPreferredScaleMac = copyPreferredScaleMac;
    callbacks.copyPreferredScaleName = copyPreferredScaleName;
    callbacks.copyScaleHistory = copyScaleHistory;
    callbacks.copyPresetBank = copyPresetBank;
    callbacks.copyRuntimeConfig = copyRuntimeConfig;
    callbacks.copyBullseyeConfig = copyBullseyeConfig;
    callbacks.stageBullseyeConfig = stageBullseyeConfig;
    callbacks.copyDebugExportExtras = copyDebugExportExtras;
    callbacks.copyTaskProfiler = copyTaskProfiler;
    if (!networkManager.begin(persistedSettings, callbacks)) {
      networkOk = false;
      logEmit(LogLevel::WARNING, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
              BOOT_SUBSYSTEM_NETWORK, 0);
    } else {
      logEmit(LogLevel::INFO, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
              BOOT_SUBSYSTEM_NETWORK, 1);
    }
  } else if (!settingsLoaded) {
    networkOk = false;
    logEmit(LogLevel::WARNING, DebugCategory::BOOT, DebugCode::BOOT_SUBSYSTEM,
            BOOT_SUBSYSTEM_NETWORK, 0);
  }
  psramOk = psramFound() && (!settingsLoaded || workBufIsExternal());
  logEmit(psramOk ? LogLevel::INFO : LogLevel::CRITICAL, DebugCategory::BOOT,
          DebugCode::BOOT_SUBSYSTEM, BOOT_SUBSYSTEM_PSRAM, psramOk ? 1 : 0);
#endif

  bootCapabilities.evaluated = true;
  bootCapabilities.platformClock = platformClockReady;
  bootCapabilities.relaySafetyTimers = relaySafetyTimersReady;
  bootCapabilities.taskWatchdog = taskWatchdogReady;
  bootCapabilities.persistentStorage = persistenceReady && flashIoReady;
  bootCapabilities.settingsLoaded = settingsLoaded && settingsDurable;
  bootCapabilities.settingsPersistenceWorker = settingsPersistenceOk;
  bootCapabilities.scaleWorker = scaleWorkerOk;
  bootCapabilities.webCommandQueue = webQueueOk;
  bootCapabilities.network = networkOk;
  bootCapabilities.psram = psramOk;
  bootCapabilities.criticalFaultLatched =
      criticalTaskWatchdogFaulted() || safetyResetStatus.recoveryRequired ||
      relaySafetyState == RelaySafetyState::LOCKOUT;
  bootState = bootCapabilities.state();
  bootDegraded = bootState == BootState::DEGRADED_SAFE ||
                 bootState == BootState::FAULT_LATCHED;
  firmwareInitializationComplete = bootState == BootState::READY;
  publishScaleWorkerPolicy(runtimeConfig, firmwareInitializationComplete);
  if (firmwareInitializationComplete) {
    addDebugEvent(DebugCategory::BOOT, DebugCode::BOOT_READY);
  } else {
    int32_t failedSubsystem = BOOT_SUBSYSTEM_SCALE_WORKER;
    if (!bootCapabilities.mandatorySafetyReady()) {
      failedSubsystem = !bootCapabilities.platformClock
                            ? BOOT_SUBSYSTEM_CPU
                            : (!bootCapabilities.relaySafetyTimers
                                   ? BOOT_SUBSYSTEM_RELAY_TIMERS
                                   : BOOT_SUBSYSTEM_TASK_WDT);
    } else if (!bootCapabilities.mandatoryDurabilityReady()) {
      failedSubsystem = !bootCapabilities.persistentStorage
                            ? BOOT_SUBSYSTEM_PERSISTENCE
                            : BOOT_SUBSYSTEM_SETTINGS_SAVE;
    }
    addDebugEvent(DebugCategory::BOOT, DebugCode::INITIALIZATION_FAILED,
                  failedSubsystem, static_cast<int32_t>(bootState));
  }
  publishControlStatus();
  serviceScaleConnectedLed();
}

void serviceHealthThresholdAlerts(uint32_t intervalMaxGapMs) {
  const bool heapLow =
      freeHeapBytes > 0 &&
      (freeHeapBytes < HEALTH_HEAP_FREE_ALERT_BYTES ||
       (largestFreeHeapBlockBytes > 0 &&
        largestFreeHeapBlockBytes < HEALTH_HEAP_LARGEST_ALERT_BYTES));
  const bool heapClear =
      freeHeapBytes == 0 ||
      (freeHeapBytes >= HEALTH_HEAP_FREE_CLEAR_BYTES &&
       (largestFreeHeapBlockBytes == 0 ||
        largestFreeHeapBlockBytes >= HEALTH_HEAP_LARGEST_CLEAR_BYTES));
  if (heapLow) {
    if (!healthHeapAlertLatched) {
      healthHeapAlertLatched = true;
      healthHeapLowSinceMs = millis();
      addDebugEvent(DebugCategory::SYSTEM, DebugCode::HEALTH_HEAP_LOW,
                    static_cast<int32_t>(freeHeapBytes),
                    static_cast<int32_t>(largestFreeHeapBlockBytes));
    }
    if (!healthHeapRestartLatched && healthHeapLowSinceMs != 0 &&
        elapsedMs(healthHeapLowSinceMs) >= HEALTH_HEAP_LOW_RESTART_MS &&
        controlAllowsConfigurationNow() && !circuitClosed) {
      healthHeapRestartLatched = true;
      addDebugEvent(DebugCategory::SYSTEM, DebugCode::HEALTH_HEAP_RESTART,
                    static_cast<int32_t>(freeHeapBytes),
                    static_cast<int32_t>(largestFreeHeapBlockBytes));
      requestSafeRestart();
    }
  } else if (heapClear) {
    healthHeapAlertLatched = false;
    healthHeapRestartLatched = false;
    healthHeapLowSinceMs = 0;
  }

  uint32_t worstStackWords = UINT32_MAX;
  if (loopStackMinWords > 0 && loopStackMinWords < worstStackWords) {
    worstStackWords = loopStackMinWords;
  }
  const uint32_t scaleStackMinWords = scaleWorkerStackMinWordsValue();
  if (scaleStackMinWords > 0 && scaleStackMinWords < worstStackWords) {
    worstStackWords = scaleStackMinWords;
  }
#ifndef SHOT_STOPPER_HOST_TEST
  const uint32_t networkStack = networkManager.snapshot().taskStackMinWords;
  if (networkStack > 0 && networkStack < worstStackWords) {
    worstStackWords = networkStack;
  }
#endif
  const bool stackLow =
      worstStackWords != UINT32_MAX &&
      worstStackWords < HEALTH_STACK_MIN_ALERT_WORDS;
  const bool stackClear =
      worstStackWords == UINT32_MAX ||
      worstStackWords >= HEALTH_STACK_MIN_CLEAR_WORDS;
  if (stackLow && !healthStackAlertLatched) {
    healthStackAlertLatched = true;
    addDebugEvent(DebugCategory::SYSTEM, DebugCode::HEALTH_STACK_LOW,
                  static_cast<int32_t>(loopStackMinWords),
                  static_cast<int32_t>(scaleStackMinWords));
  } else if (stackClear) {
    healthStackAlertLatched = false;
  }

  const bool gapHigh = intervalMaxGapMs >= HEALTH_LOOP_GAP_ALERT_MS;
  const bool gapClear = intervalMaxGapMs <= HEALTH_LOOP_GAP_CLEAR_MS;
  if (gapHigh && !healthLoopGapAlertLatched) {
    healthLoopGapAlertLatched = true;
    addDebugEvent(DebugCategory::SYSTEM, DebugCode::HEALTH_LOOP_GAP,
                  static_cast<int32_t>(intervalMaxGapMs), 0);
  } else if (gapClear) {
    healthLoopGapAlertLatched = false;
  }
}

// cppcheck-suppress unusedFunction ; Arduino framework entry point
void loop() {
  const uint32_t loopStartedAtMs = millis();
  const uint32_t loopStartedAtUs = micros();
  recordResetUptime(loopStartedAtMs);
  if (lastLoopAtMs != 0) {
    const uint32_t gap = loopStartedAtMs - lastLoopAtMs;
    if (gap > loopMaxGapMs) {
      loopMaxGapMs = gap;
    }
    if (gap > CONTROL_SERVICE_DEADLINE_MS) {
      ++loopDeadlineMisses;
    }
    if (gap > healthIntervalMaxGapMs) {
      healthIntervalMaxGapMs = gap;
    }
  }
  lastLoopAtMs = loopStartedAtMs;
  // Relay and paddle control never wait for BLE. The worker owns every scale,
  // heartbeat, packet, timer and connection operation. Restart before heap
  // walks: a failed Wi-Fi stop can leave TLSF unwalkable.
  serviceRelaySafety();
  if (safeRestartPending()) {
    const bool faultRestart =
        criticalTaskWatchdogFaulted();
    if (faultRestart ||
        (!session.active && !getRelaySafetySnapshot().closed)) {
      machineRequestStop();
      serviceSafetyHeartbeat(false);
#ifndef SHOT_STOPPER_HOST_TEST
      recordResetUptime(millis(), true);
      ESP.restart();
#else
      (void)consumeSafeRestartRequest();
#endif
      return;
    }
    // Planned restart waits for the shot. Fault paths above still open K1
    // and reset immediately so a hung firmware cannot keep the group on.
  }
  if (elapsedMs(healthTelemetryAtMs) >= HEALTH_TELEMETRY_INTERVAL_MS) {
    const uint32_t intervalMs = elapsedMs(healthTelemetryAtMs);
    healthTelemetryAtMs = loopStartedAtMs;
    loopStackMinWords =
        static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
#ifndef SHOT_STOPPER_HOST_TEST
    const HeapCapSnapshot heap = sampleHeapCaps();
    freeHeapBytes = heap.internalFree;
    minimumFreeHeapBytes = heap.internalMinimum;
    largestFreeHeapBlockBytes = heap.internalLargest;
    psramSizeBytes = heap.psramTotal;
    psramFreeBytes = heap.psramFree;
    psramLargestFreeBlockBytes = heap.psramLargest;
    const ShotStopperBleHealth bleRuntime = shotStopperBleRuntimeHealth();
    bleRuntimeState = static_cast<uint8_t>(bleRuntime.state);
    bleRuntimeLastError = bleRuntime.lastError;
    bleRuntimeLastResetReason = bleRuntime.lastResetReason;
    bleRuntimeSyncGeneration = bleRuntime.syncGeneration;
    bleRuntimeResetCount = bleRuntime.resetCount;
    bleRuntimeHostStackMinWords = bleRuntime.hostTaskStackHighWaterWords;
    hwmonSnapshot = hwmon.sample(intervalMs > 0U ? intervalMs
                                                 : HEALTH_TELEMETRY_INTERVAL_MS,
                                 &heap);
#else
    hwmonSnapshot = hwmon.sample(intervalMs > 0U ? intervalMs
                                                 : HEALTH_TELEMETRY_INTERVAL_MS);
#endif
    serviceHealthThresholdAlerts(healthIntervalMaxGapMs);
    loopIntervalGapMs = healthIntervalMaxGapMs;
    healthIntervalMaxGapMs = 0;
  }
  pushActivatorDrivePermission();
  machineSampleInput();
  processScaleWorkerEvents();
  observeMachineSenseFromSession();
  serviceMachine();
  captureLoopGuards();
  serviceNoScaleShotGuard(loopGuardInputs);
  serviceCupStartGuard(loopGuardInputs);
  stateMachineTask();
  publishControlCriticalLoggingState();
  serviceWebhookBrewStart();
  machineOnBrewOutcome(session.active);
  serviceExtendedPulseAlert();
  localBuzzer.service(millis());
  servicePendingBrewRfRestore();
  serviceScaleCompletionBeep();
  servicePendingScaleTimerStop();
  serviceRemoteTimerStopRetry();
  serviceBullseyeMelody();
  pendingShotFinalizeTask();
  serviceSerialLogDump();
  serviceSerialCli();
  serviceMaintenanceCancellation();
  processBleCompanionRequests();
  processWebCommands();
  taskProfiler.service(millis());
  serviceControlCommandResult();
  serviceRuntimePersistence();
  serviceShotStorePersistence();
  servicePreferredScaleMacPersistence();
  serviceMaintenanceLease();
  servicePendingPlannedRestart();
  serviceControlStatusPublish();
  publishBleCompanionRuntimeSnapshot();
  serviceScaleConnectedLed();
  if (!feedCurrentTaskWatchdog()) {
    reportTaskWatchdogFault();
    tripRelaySafety(RelaySafetyFault::TASK_WATCHDOG_FAILURE);
    requestSafeRestart();
    serviceSafetyHeartbeat(false);
    return;
  }
  serviceSafetyHeartbeat(true);
  processScaleWorkerEvents();
  const uint32_t executionUs = micros() - loopStartedAtUs;
  if (executionUs > loopMaxExecutionUs) {
    loopMaxExecutionUs = executionUs;
  }
  vTaskDelay(pdMS_TO_TICKS(controlLoopTickDelayMs()));
}
