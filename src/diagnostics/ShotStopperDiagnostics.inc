bool liveConfigPersistPending() {
#ifndef SHOT_STOPPER_HOST_TEST
  settingsPersistMux.lock();
  const bool pending = runtimePersistPending || settingsPersistInFlight ||
                       settingsPersistResultReady;
  settingsPersistMux.unlock();
  return pending;
#else
  return runtimePersistPending;
#endif
}

void publishControlGate() {
  ControlGateSnapshot next;
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  next.state = stopperState;
  next.activeCycle = session.active;
  next.relayClosed = relay.closed;
  next.machineRunning = machineIsRunning();
  next.physicalActivatorOn = rawActivatorOn;
  next.maintenanceLeaseActive = maintenanceLease.active;
  next.maintenanceLeaseId = maintenanceLease.active ? maintenanceLease.id : 0;
  next.source = session.active ? session.source : ControlSource::NONE;
  {
    TaskLockGuard lock(controlGateMutex);
    publishedControlGate = next;
  }
#if !defined(SHOT_STOPPER_HOST_TEST)
  networkManager.syncControlCriticalRf(next.activeCycle || next.relayClosed);
#endif
}

void publishControlStatus() {
  const uint32_t now = millis();
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  const ScaleLinkSnapshot scaleLink = getScaleLinkSnapshot();
  const MachineStatusSample machine = machineSampleStatus();
  serviceWeightStreamTelemetry();
  const bool weightFresh =
      scaleLinkAvailable(scaleLink) && currentWeightIsFresh(now);
  // Gather other owners before locking publication. Waiting for one producer
  // must not prevent readers from using the last committed control snapshot.
  char preferredMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  char preferredName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  copyPreferredScaleMac(preferredMac, sizeof(preferredMac));
  copyPreferredScaleName(preferredName, sizeof(preferredName));
  const uint32_t macPauseRemainingMs = scaleMacCachePauseRemainingMs(now);
  const bool persistPending = liveConfigPersistPending();
  const BleCompanionStatusSnapshot ble = copyBleCompanionStatus();
  const CupTareDiagnostics cupTare = captureCupTareDiagnostics();
  TaskLockGuard lock(controlStatusMutex);
  ControlStatusSnapshot &next = publishedControlStatus;
  next = ControlStatusSnapshot{};
  next.cupTare = cupTare;
  next.snapshotVersion = ++controlStatusVersion;
  if (controlStatusVersion == 0) {
    controlStatusVersion = 1;
    next.snapshotVersion = controlStatusVersion;
  }
  next.state = stopperState;
  next.activeCycle = session.active;
  next.relayClosed = relay.closed;
  machineFillStatus(next, machine);
  next.virtualHoldOn = virtualHoldOn;
  next.remoteControlEnabled = REMOTE_MACHINE_CONTROL_ENABLED;
  next.source = session.active ? session.source : ControlSource::NONE;
  next.cycleId = session.active ? session.id : 0;
  next.bootId = shotLog.bootId();
  next.maintenanceLeaseActive = maintenanceLease.active;
  next.maintenanceLeaseId = maintenanceLease.active ? maintenanceLease.id : 0;
  next.maintenanceStartedAtMs =
      maintenanceLease.active ? maintenanceLease.startedAtMs : 0;
  next.safetyState = relay.state;
  next.safetyFault = relay.fault;
  next.safetyGeneration = relay.generation;
  next.safetyTimersReady = relay.timersReady;
  next.taskWatchdogReady = relay.watchdogReady;
  next.externalSafetyPresent = relay.externalSafetyPresent;
  next.circuitFeedbackClosed = relay.feedbackClosed;
  next.resetReasonCode = relay.resetReasonCode;
  next.unsafeResetCount = relay.unsafeResetCount;
  next.resetRecoveryRequired = relay.resetRecoveryRequired;
  next.bootLoopDetected = relay.bootLoopDetected;
  next.resetHistoryCount = relay.resetHistoryCount;
  for (uint8_t i = 0; i < next.resetHistoryCount; ++i)
    next.resetHistory[i] = relay.resetHistory[i];
  next.scaleAvailable = scaleLinkAvailable(scaleLink);
  next.weightControlState = session.active
                                ? session.weightControlState
                                : WeightControlState::INACTIVE;
  next.weightStreamState = telemetryWeightStreamState;
  next.scaleRecoveredStaleCount = scaleRecoveredStaleCount;
  next.scaleRecoveredStaleMs = scaleRecoveredStaleMs;
  next.currentWeightValid = weightFresh;
  next.currentWeightG = next.currentWeightValid ? currentWeight : 0.0f;
  next.currentWeightAgeMs =
      next.currentWeightValid
          ? static_cast<uint32_t>(now - currentWeightReceivedAtMs)
          : 0;
  next.observedWeightValid = observedWeightSequence > 0 &&
                             std::isfinite(observedWeight) &&
                             fabsf(observedWeight) <= MAX_PARSED_WEIGHT_G;
  next.observedWeightG = next.observedWeightValid ? observedWeight : 0.0f;
  next.observedWeightAgeMs = next.observedWeightValid
                                 ? elapsedMs(observedWeightReceivedAtMs)
                                 : 0;
  next.currentTimerValid = next.scaleAvailable && scaleLink.timerValid;
  next.currentTimerMs = next.currentTimerValid ? scaleLink.timerMs : 0;
  next.currentTimerAgeMs = next.currentTimerValid ? scaleLink.timerAgeMs : 0;
  next.scaleConnectionGeneration = scaleLink.connectionGeneration;
  next.scalePacketSequence = scaleLink.packetSequence;
  next.scalePacketGaps = scaleLink.packetGaps;
  next.scaleWeightUpdateIntervalMs = scaleLink.weightUpdateIntervalMs;
  next.scaleRejectedPackets = scaleLink.rejectedPackets;
  next.scaleReconnects = scaleLink.reconnects;
  next.scaleLastDisconnectReason = scaleLink.lastDisconnectReason;
  next.scaleBleDiagnostics = scaleLink.bleDiagnostics;
  next.scaleRssiValid = scaleLink.rssiValid;
  next.scaleRssi = scaleLink.rssi;
  next.uptimeMs = elapsedMs(bootStartedAtMs);
  next.loopIntervalGapMs =
      healthIntervalMaxGapMs > loopIntervalGapMs ? healthIntervalMaxGapMs
                                                 : loopIntervalGapMs;
  next.loopMaxGapMs = loopMaxGapMs;
  next.loopDeadlineMisses = loopDeadlineMisses;
  next.loopMaxExecutionUs = loopMaxExecutionUs;
  next.scaleWorkerMaxGapMs = scaleWorkerMaxGapMsValue();
  next.scaleWorkerDeadlineMisses = scaleWorkerDeadlineMissCount();
  next.scaleWorkerMaxExecutionUs = scaleWorkerMaxExecutionUsValue();
  next.loopStackMinBytes = loopStackMinBytes;
  next.scaleStackMinBytes = scaleWorkerStackMinBytesValue();
  next.freeHeapBytes = freeHeapBytes;
  next.minimumFreeHeapBytes = minimumFreeHeapBytes;
  next.largestFreeHeapBlockBytes = largestFreeHeapBlockBytes;
  next.psramSizeBytes = psramSizeBytes;
  next.psramFreeBytes = psramFreeBytes;
  next.psramMinimumFreeBytes = psramMinimumFreeBytes;
  next.psramLargestFreeBlockBytes = psramLargestFreeBlockBytes;
  next.bleHostAllocPsramCount = bleHostAllocPsramCount;
  next.bleHostAllocFallbackCount = bleHostAllocFallbackCount;
  next.bleHostHciRxDropped = bleHostHciRxDropped;
  next.bleHostHciTxDropped = bleHostHciTxDropped;
  next.bleBackend = 2;
  next.bleRuntimeState = bleRuntimeState;
  next.bleRuntimeLastError = bleRuntimeLastError;
  next.bleRuntimeLastResetReason = bleRuntimeLastResetReason;
  next.bleRuntimeSyncGeneration = bleRuntimeSyncGeneration;
  next.bleRuntimeResetCount = bleRuntimeResetCount;
  next.bleRuntimeHostStackMinBytes = bleRuntimeHostStackMinBytes;
  next.workBufExternal = workBufIsExternal();
#ifndef SHOT_STOPPER_HOST_TEST
  next.jsonArenaExternal = jsonArenaIsExternal();
#else
  next.jsonArenaExternal = false;
#endif
  next.allocExternalFallbackCount = allocExternalFallbackCount();
  next.hwmon = hwmonSnapshot;
  next.scaleEventsDropped = scaleWorkerDroppedEventCount();
  next.config = effectiveRuntimeConfig();
  next.bbwEvidenceCount =
      bbwLearningBank.forPreset(presetBank.activeId, presetBank).evidence.count;
  const ShotPreset &bbwPreset = activeShotPreset(presetBank);
  next.bbwPresetId = bbwPreset.id;
  next.bbwAlpha = bbwPreset.bbwEwmaAlpha;
  next.bbwAlphaBaseline = bbwPreset.bbwAlphaBaseline;
  next.bbwAlphaLearned = bbwPreset.bbwAlphaLearned != 0;
  next.bbwLegacyOffsetG = bbwPreset.weightOffsetG;
  next.bbwEwmaOffsetG = bbwPreset.bbwEwmaOffsetG;
  next.lastCycle = lastCycle;
  next.lastShot = persistedLastShot;
  {
    const ShotCurveRecord curve =
        session.active ? shotCurveSampler.snapshot() : lastShotCurve;
    copyShotCurveRecordToStatusFields(
        curve, next.shotCurveCount, next.shotCurveIntervalS,
        next.shotCurveFirstDropDs, next.shotCurveFirstDropCg,
        next.shotCurveExtendedDs, next.shotCurveExtendedCg,
        next.shotCurveAtmDs, next.shotCurveAtmCg,
        next.shotCurveAtmClearedDs, next.shotCurveEndedDs,
        next.shotCurveEndedCg, next.shotCurveWeightCg,
        sizeof(next.shotCurveWeightCg) / sizeof(next.shotCurveWeightCg[0]));
  }
  copyCString(next.scaleProtocol, sizeof(next.scaleProtocol),
              scaleLink.protocolName);
  copyCString(next.preferredScaleMac, sizeof(next.preferredScaleMac), preferredMac);
  copyCString(next.preferredScaleName, sizeof(next.preferredScaleName), preferredName);
  next.scaleMacCachePauseRemainingMs = macPauseRemainingMs;
  if (session.active) {
    next.cycleFlowDuringRetare = session.flowDuringRetare;
    next.cycleRetarePerformed = session.retarePerformed;
    next.cycleStartedWithScale = session.startedWithScale;
    next.cycleAutomaticBrew = shot.automaticBrew;
    next.cycleTimerOnly = session.config.timerOnly;
    next.cycleFirstDropMs = session.firstDropMs;
    next.cycleRetareFlowFirstDetectedAtMs =
        session.retareFlowFirstDetectedAtMs;
    next.cycleStartedAtMs = session.circuitClosedAtMs != 0U
                                ? session.circuitClosedAtMs
                                : session.startedAtMs;
    next.cycleElapsedMs = machine.circuitElapsedMs;
    next.cycleExtractionExtended =
        session.extractionExtended && fastExtractionGuardSession();
    next.cycleSlowExtractionExtended =
        session.slowExtractionExtended && slowExtractionGuardSession();
    next.cycleTargetReachedEarly = session.targetReachedEarly;
    if (next.cycleExtractionExtended) {
      next.cycleActiveStopWeightG = session.config.maxRecoveryWeightG;
      const uint32_t elapsed = next.cycleElapsedMs;
      next.cycleMinBbwBrewTimeRemainingMs =
          elapsed >= session.config.minBbwBrewTimeMs
              ? 0U
              : session.config.minBbwBrewTimeMs - elapsed;
    } else if (next.cycleSlowExtractionExtended) {
      next.cycleActiveStopWeightG = session.config.minRecoveryWeightG;
      next.cycleMinBbwBrewTimeRemainingMs = 0;
    } else {
      next.cycleActiveStopWeightG =
          static_cast<float>(session.config.goalWeightG);
      next.cycleMinBbwBrewTimeRemainingMs = 0;
    }
    next.cycleAutoToManualGuardArmed = session.autoToManualGuardArmed;
    next.cycleAutoToManualGuardEnforced = session.autoToManualGuardEnforced;
    next.cycleAccidentalTouchHolding = session.accidentalTouchHolding;
    next.cycleAccidentalTouchPhase =
        static_cast<uint8_t>(session.accidentalTouchPhase);
    next.cycleAccidentalTouchClass =
        static_cast<uint8_t>(session.accidentalTouchClass);
    next.cycleAccidentalTouchPendingCount = session.accidentalTouchPendingCount;
    next.cycleCupRemovedPending = session.cupRemovedPending;
    next.cycleBbwProtectionEnabled = session.bbwProtectionEnabled;
    next.cycleBbwProtectionEnded = session.bbwProtectionEnded;
    if (session.autoToManualGuardEnforced) {
      const uint32_t nowMs = millis();
      next.cycleAutoToManualGuardRemainingMs =
          static_cast<int32_t>(session.autoToManualGuardDeadlineAtMs -
                               nowMs) <= 0
              ? 0U
              : session.autoToManualGuardDeadlineAtMs - nowMs;
    }
  }
  {
    const ShotPreset &active = activeShotPreset(presetBank);
    next.autoToManualGuardTrendMs = autoToManualGuardTrendMs(
        active.autoToManualGuardSamplesDs, active.operationalWallMs);
  }
  next.noScaleShotGuardEnabled = noScaleBbwEnabled(runtimeConfig.noScaleBbwMode);
  next.noScaleShotGuardArmed = noScaleShotGuardArmed;
  next.noScaleShotGuardHold = noScaleShotGuardHold;
  next.noScaleShotGuardScaleWasAvailable = noScaleShotGuardScaleWasAvailable;
  next.noScaleShotGuardCooldownRemainingMs = 0;
  if (noScaleBbwEnabled(runtimeConfig.noScaleBbwMode) &&
      !noScaleShotGuardArmed && noScaleShotGuardActivityAtMs != 0) {
    const uint32_t elapsed = elapsedMs(noScaleShotGuardActivityAtMs);
    next.noScaleShotGuardCooldownRemainingMs =
        elapsed >= runtimeConfig.lastShotCooldownMs
            ? 0U
            : runtimeConfig.lastShotCooldownMs - elapsed;
  }
  next.cupStartGuardHold = cupStartGuardHold;
  next.machineRunState = machine.runState;
  next.cupPresenceState = cupPresenceState();
  next.cupPresent = cupPresenceState() == CupPresenceState::PRESENT;
  next.configPersistPending = persistPending;
  next.configPersistFailed = runtimePersistFailed;
  next.bootCapabilities = bootCapabilities;
  next.bootCapabilities.criticalFaultLatched =
      next.bootCapabilities.criticalFaultLatched ||
      criticalTaskWatchdogFaulted() || relay.resetRecoveryRequired ||
      relay.state == RelaySafetyState::LOCKOUT;
  next.bootState = next.bootCapabilities.state();
  next.bootComplete = next.bootState == BootState::READY;
  next.bootDegraded = next.bootState == BootState::DEGRADED_SAFE ||
                      next.bootState == BootState::FAULT_LATCHED;
  next.scaleWorkerReady = scaleWorkerReady();
  next.usbConsoleIo4Closed = usbConsoleJumperPresent();
  next.usbSerialEnableSource = usbSerialEnableSource;
  next.bleCompanionResultDropped = bleCompanionResultDropped;
  {
    next.bleCompanionEnabled = ble.configuredEnabled;
    next.bleCompanionActive = ble.enabled;
    next.bleCompanionRestartRequired = ble.restartRequired;
    next.bleCompanionStackReady = ble.stackReady;
    next.bleCompanionAdvertising = ble.advertising;
    next.bleCompanionConnected = ble.connected;
    next.bleCompanionProtocolVersion = ble.protocolVersion;
    next.bleCompanionAcceptedWrites = ble.acceptedWrites;
    next.bleCompanionRejectedWrites = ble.rejectedWrites;
    next.bleCompanionLastReject = static_cast<uint8_t>(ble.lastReject);
    next.bleCompanionLastRawError = ble.lastRawError;
    next.bleCompanionAdvertisingStarts = ble.advertisingStarts;
    next.bleCompanionAdvertisingFailures = ble.advertisingFailures;
    next.bleCompanionPhoneConnects = ble.phoneConnects;
    next.bleCompanionPhoneDisconnects = ble.phoneDisconnects;
    next.bleCompanionScanIntensity =
        static_cast<uint8_t>(liveBleScanIntensity());
  }
  next.debugEventsDropped =
      __atomic_load_n(&debugLogDroppedSnapshot, __ATOMIC_RELAXED) +
      __atomic_load_n(&debugLogContentionDropped, __ATOMIC_RELAXED) +
      serialLogDroppedCount();
  lock.unlock();
  publishControlGate();
}

void refreshControlStatus() {
#if defined(SHOT_STOPPER_HOST_TEST)
  return;
#else
  // Waits until loop commits the requested status snapshot, not merely until
  // the request is noticed. Timeout leaves the request flag set.
  // is noticed. Timeout copies the last snapshot and leaves the flag set.
  __atomic_store_n(&controlStatusPublishRequested, true, __ATOMIC_RELEASE);
  const uint32_t startedAtMs = millis();
  while (__atomic_load_n(&controlStatusPublishRequested, __ATOMIC_ACQUIRE)) {
    if (static_cast<uint32_t>(millis() - startedAtMs) >=
        CONTROL_STATUS_REFRESH_WAIT_MS) {
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
#endif
}

void serviceControlStatusPublish() {
  publishControlGate();
  serviceWeightStreamTelemetry();
  const RelaySafetySnapshot relaySnap = getRelaySafetySnapshot();
  const bool requested =
      __atomic_load_n(&controlStatusPublishRequested, __ATOMIC_ACQUIRE);
  const bool forcePublish =
      publishedControlStatus.state != stopperState ||
      publishedControlStatus.activeCycle != session.active ||
      publishedControlStatus.relayClosed != relaySnap.closed ||
      publishedControlStatus.machineRunning != machineIsRunning() ||
      publishedControlStatus.safetyGeneration != relaySnap.generation ||
      publishedControlStatus.safetyFault != relaySnap.fault ||
      publishedControlStatus.configPersistPending !=
          liveConfigPersistPending() ||
      publishedControlStatus.configPersistFailed != runtimePersistFailed;
  if (requested || forcePublish) {
    publishControlStatus();
  }
  // Clear after the mutex-protected commit so httpd's wait cannot copy the
  // previous blob. A GET that arrives mid-publish blocks until that snapshot
  // is complete.
  if (requested) {
    __atomic_store_n(&controlStatusPublishRequested, false, __ATOMIC_RELEASE);
  }
}

SerialCliParser serialCliParser;

void resetSerialCliState() {
  serialCliResetParser(serialCliParser);
}

void serialCliReply(const char *message) {
  Serial.println(message);
}

void serialCliWipeCommandSecrets(WebCommand &command) {
  command.wipePassword();
}

void serialCliRejectUnsafe() {
  serialCliReply(
      "ERR not ready: paddle OFF, machine circuit open, Ready, no active cycle");
}

void serialCliQueueCommand(WebCommand &command, SerialCliVerb verb) {
  command.requestId = millis();
  if (!enqueueWebCommand(command)) {
    serialCliWipeCommandSecrets(command);
    serialCliReply("ERR control queue full");
    return;
  }
  serialCliWipeCommandSecrets(command);
  char line[48] = {};
  snprintf(line, sizeof(line), "OK queued %s", serialCliVerbName(verb));
  serialCliReply(line);
}

void serialCliQueueIfSafe(WebCommand &command, SerialCliVerb verb) {
  if (!controlAllowsConfigurationNow()) {
    serialCliWipeCommandSecrets(command);
    serialCliRejectUnsafe();
    return;
  }
  serialCliQueueCommand(command, verb);
}

void serialCliWarnIfCycleActive() {
  if (session.active) {
    serialCliReply("WARN cycle active; proceeding");
  }
}

void serialCliQueueNetworkAction(WebCommandType type, SerialCliVerb verb) {
  serialCliWarnIfCycleActive();
  WebCommand command;
  command.type = type;
  command.requestId = millis();
  if (!forwardAcceptedNetworkCommand(command)) {
    serialCliReply("ERR network queue full");
    return;
  }
  char line[48] = {};
  snprintf(line, sizeof(line), "OK queued %s", serialCliVerbName(verb));
  serialCliReply(line);
}

void serialCliFillNetworkDump(SerialCliNetworkDump &dump) {
#ifndef SHOT_STOPPER_HOST_TEST
  const NetworkStatusSnapshot snap = networkManager.snapshot();
  dump.networkActive = snap.networkActive;
  dump.apActive = snap.apActive;
  dump.httpActive = snap.httpActive;
  dump.wifiConfigured = snap.wifiConfigured;
  dump.staOpen = snap.staOpen;
  dump.staLinkMetricsValid = snap.staLinkMetricsValid;
  dump.staReconnectHeld = snap.staReconnectHeld;
  dump.apStartHeld = snap.apStartHeld;
  dump.httpStartHeld = snap.httpStartHeld;
  dump.devicePasswordFactory = snap.devicePasswordFactory;
  dump.ntpMayArm = snap.ntpMayArm;
  dump.apClients = snap.apClients;
  dump.staState = static_cast<uint8_t>(snap.staState);
  dump.staIpMode = snap.staIpMode;
  dump.staConfigState = snap.staConfigState;
  dump.wifiMode = snap.wifiMode;
  dump.channel = snap.channel;
  dump.scanState = snap.scanState;
  dump.ntpState = snap.ntpState;
  dump.staRssi = snap.staRssi;
  dump.staSignalQualityPct = snap.staSignalQualityPct;
  dump.wifiStatus = snap.wifiStatus;
  dump.confirmRemainingMs = snap.confirmRemainingMs;
  dump.taskAgeMs = snap.taskAgeMs;
  dump.taskStackMinBytes = snap.taskStackMinBytes;
  dump.startupFailures = snap.startupFailures;
  dump.lastCommandRequestId = snap.lastCommandRequestId;
  dump.staConnectAgeMs = snap.staConnectAgeMs;
  dump.staReconnectAgeMs = snap.staReconnectAgeMs;
  dump.lastCommandState = snap.lastCommandState;
  copyCString(dump.apIp, sizeof(dump.apIp), snap.apIp);
  copyCString(dump.staIp, sizeof(dump.staIp), snap.staIp);
  copyCString(dump.staSsid, sizeof(dump.staSsid), snap.staSsid);
  copyCString(dump.configuredIp, sizeof(dump.configuredIp), snap.configuredIp);
  copyCString(dump.configuredNetmask, sizeof(dump.configuredNetmask),
              snap.configuredNetmask);
  copyCString(dump.configuredGateway, sizeof(dump.configuredGateway),
              snap.configuredGateway);
  copyCString(dump.configuredDns1, sizeof(dump.configuredDns1),
              snap.configuredDns1);
  copyCString(dump.configuredDns2, sizeof(dump.configuredDns2),
              snap.configuredDns2);
  copyCString(dump.staMac, sizeof(dump.staMac), snap.staMac);
  copyCString(dump.staBssid, sizeof(dump.staBssid), snap.staBssid);
  copyCString(dump.apMac, sizeof(dump.apMac), snap.apMac);
  copyCString(dump.ntpActiveServer, sizeof(dump.ntpActiveServer),
              snap.ntpActiveServer);
#else
  (void)dump;
#endif
}

void serialCliPrintLiveNetworkStatus(SerialCliVerb verb) {
  SerialCliNetworkDump dump;
  serialCliFillNetworkDump(dump);
  if (verb == SerialCliVerb::WIFI_STATUS) {
    serialCliPrintWifiStatus(dump);
  } else if (verb == SerialCliVerb::AP_STATUS) {
    serialCliPrintApStatus(dump);
  } else if (verb == SerialCliVerb::WEBUI_STATUS) {
    serialCliPrintWebuiStatus(dump);
  } else {
    serialCliPrintNetStatus(dump);
  }
}

void serialCliPrintLiveHealth() {
  SerialCliHealthDump dump;
  dump.freeHeapBytes = freeHeapBytes;
  dump.minimumFreeHeapBytes = minimumFreeHeapBytes;
  dump.largestFreeHeapBlockBytes = largestFreeHeapBlockBytes;
  dump.psramSizeBytes = psramSizeBytes;
  dump.psramFreeBytes = psramFreeBytes;
  dump.psramLargestFreeBlockBytes = psramLargestFreeBlockBytes;
  dump.bleHostAllocPsramCount = bleHostAllocPsramCount;
  dump.bleHostAllocFallbackCount = bleHostAllocFallbackCount;
  dump.bleHostHciRxDropped = bleHostHciRxDropped;
  dump.bleHostHciTxDropped = bleHostHciTxDropped;
  dump.bleBackend = 2;
  dump.bleRuntimeState = bleRuntimeState;
  dump.bleRuntimeLastError = bleRuntimeLastError;
  dump.bleRuntimeLastResetReason = bleRuntimeLastResetReason;
  dump.bleRuntimeSyncGeneration = bleRuntimeSyncGeneration;
  dump.bleRuntimeResetCount = bleRuntimeResetCount;
  dump.bleRuntimeHostStackMinBytes = bleRuntimeHostStackMinBytes;
  dump.workBufExternal = workBufIsExternal();
#ifndef SHOT_STOPPER_HOST_TEST
  dump.jsonArenaExternal = jsonArenaIsExternal();
#else
  dump.jsonArenaExternal = false;
#endif
  dump.allocExternalFallbackCount = allocExternalFallbackCount();
  dump.loopMaxGapMs = loopMaxGapMs;
  dump.healthIntervalMaxGapMs =
      healthIntervalMaxGapMs > loopIntervalGapMs ? healthIntervalMaxGapMs
                                                 : loopIntervalGapMs;
  dump.loopStackMinBytes = loopStackMinBytes;
  dump.scaleWorkerStackMinBytes = scaleWorkerStackMinBytesValue();
#ifndef SHOT_STOPPER_HOST_TEST
  dump.networkStackMinBytes = networkManager.snapshot().taskStackMinBytes;
#endif
  dump.heapAlertLatched = healthHeapAlertLatched;
  dump.stackAlertLatched = healthStackAlertLatched;
  dump.loopGapAlertLatched = healthLoopGapAlertLatched;
  dump.cpuLoadValid = hwmonSnapshot.cpuLoadValid;
  dump.cpuMhz = hwmonSnapshot.cpuMhz;
  dump.cpuLoad5s = hwmonSnapshot.cpuLoad5s;
  dump.cpuLoad1m = hwmonSnapshot.cpuLoad1m;
  dump.cpuLoad5m = hwmonSnapshot.cpuLoad5m;
  dump.cpu0Busy = hwmonSnapshot.cpu0Busy;
  dump.cpu1Busy = hwmonSnapshot.cpu1Busy;
  dump.tempValid = hwmonSnapshot.tempValid;
  dump.tempC = hwmonSnapshot.tempC;
  dump.tempPeakC = hwmonSnapshot.tempPeakC;
  serialCliPrintHealth(dump);
}

void serialCliPrintLiveScaleStatus() {
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  SerialCliScaleDump dump;
  dump.state = link.state == ScaleLinkState::CONNECTED ? "CONNECTED"
                                                       : "DISCONNECTED";
  dump.protocolName = link.protocolName;
  copyPreferredScaleMac(dump.preferredMac, sizeof(dump.preferredMac));
  copyPreferredScaleName(dump.preferredName, sizeof(dump.preferredName));
  dump.disconnectSequence = link.disconnectSequence;
  dump.connectionGeneration = link.connectionGeneration;
  dump.packetSequence = link.packetSequence;
  dump.packetGaps = link.packetGaps;
  dump.rejectedPackets = link.rejectedPackets;
  dump.reconnects = link.reconnects;
  dump.recoveredStaleCount = scaleRecoveredStaleCount;
  dump.recoveredStaleMs = scaleRecoveredStaleMs;
  dump.lastDisconnectReason = link.lastDisconnectReason;
  dump.rssiValid = link.rssiValid;
  dump.rssi = link.rssi;
  dump.workerAgeMs = elapsedMs(link.workerProgressAtMs);
  dump.timerValid = link.timerValid;
  dump.timerMs = link.timerMs;
  dump.timerAgeMs = link.timerAgeMs;
  dump.weightFresh = currentWeightIsFresh();
  dump.currentWeightG = currentWeight;
  serialCliPrintScaleStatus(dump);
}

void serialCliPrintLiveNtpStatus() {
  const TimeStatusSnapshot time = g_wallClock.snapshot(millis());
  SerialCliNtpDump dump;
  dump.state = time.state;
  dump.utcSec = time.utcSec;
  dump.lastSyncAgeMs = time.lastSyncAgeMs;
  dump.nextRetryInMs = time.nextRetryInMs;
  dump.consecutiveFailures = time.consecutiveFailures;
  copyCString(dump.activeServer, sizeof(dump.activeServer), time.activeServer);
  dump.ntpServerPreset = runtimeConfig.ntpServerPreset;
  copyCString(dump.ntpServerCustom, sizeof(dump.ntpServerCustom),
              runtimeConfig.ntpServerCustom);
  dump.timezoneOffsetMinutes = runtimeConfig.timezoneOffsetMinutes;
#ifndef SHOT_STOPPER_HOST_TEST
  dump.staUp = networkManager.snapshot().staState == StaState::CONNECTED;
#else
  dump.staUp = false;
#endif
  serialCliPrintNtpStatus(dump);
}

void serialCliPrintBleCompanionStatus() {
  const BleCompanionStatusSnapshot ble = copyBleCompanionStatus();
  Serial.println("BLE_COMPAT_STATUS");
  Serial.print("configuredEnabled=");
  Serial.println(ble.configuredEnabled ? 1 : 0);
  Serial.print("activeThisBoot=");
  Serial.println(ble.enabled ? 1 : 0);
  Serial.print("restartRequired=");
  Serial.println(ble.restartRequired ? 1 : 0);
  Serial.print("stackReady=");
  Serial.println(ble.stackReady ? 1 : 0);
  Serial.print("advertising=");
  Serial.println(ble.advertising ? 1 : 0);
  Serial.print("connected=");
  Serial.println(ble.connected ? 1 : 0);
  Serial.print("protocol=");
  Serial.println(ble.protocolVersion);
  Serial.print("apActive=");
  Serial.println(ble.apActive ? 1 : 0);
  Serial.print("acceptedWrites=");
  Serial.println(ble.acceptedWrites);
  Serial.print("rejectedWrites=");
  Serial.println(ble.rejectedWrites);
  Serial.print("lastReject=");
  Serial.println(bleCompanionRejectReasonName(ble.lastReject));
  Serial.print("scanIntensity=");
  Serial.println(bleScanIntensityName(liveBleScanIntensity()));
}

void serialCliPrintLiveLogDump() {
  if (session.active || circuitClosed) {
    serialCliReply("ERR LOG dump deferred; circuit/cycle active");
    return;
  }
  serialLogDumpCount = copyDebugEvents(
      0, serialLogDumpSnapshot, DEBUG_EVENT_CAPACITY, nullptr);
  serialLogDumpIndex = 0;
  serialCliPrintLogDumpPreamble(serialLogDumpCount,
                                currentRingRetainLogLevel());
#if defined(SHOT_STOPPER_HOST_TEST)
  // The host sink is an in-memory stub and cannot block. Preserve the
  // synchronous CLI contract used by portable tests.
  while (serialLogDumpIndex < serialLogDumpCount) {
    writeSerialLogLine(serialLogDumpSnapshot[serialLogDumpIndex++]);
  }
#endif
}

void serviceSerialLogDump() {
#if !defined(SHOT_STOPPER_HOST_TEST)
  // One bounded record per control iteration. Formatting is local and the
  // sink only attempts a zero-timeout queue send; USB I/O belongs to
  // serial_log on core 0.
  if (serialLogDumpIndex < serialLogDumpCount) {
    writeSerialLogLine(serialLogDumpSnapshot[serialLogDumpIndex++]);
  }
#endif
}

void serialCliApplyDebugPersist(bool serialOn, LogLevel ringLevel,
                                const char *okMessage) {
  if (!settingsPersistenceAvailable()) {
    serialCliReply("ERR settings persistence unavailable");
    return;
  }
  if (!serialOn) {
    serialCliReply(okMessage);
  }
  RuntimeConfig candidate = runtimeConfig;
  setSerialLogLevel(candidate,
                    serialOn ? (ringLevel == LogLevel::DEBUG ? LogLevel::DEBUG
                                                              : LogLevel::INFO)
                             : LogLevel::NONE);
  candidate.ringRetainLogLevel = static_cast<uint8_t>(ringLevel);
  ++candidate.revision;
  if (candidate.revision == 0) {
    candidate.revision = 1;
  }
  commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_USER);
  if (serialOn) {
    serialCliReply(okMessage);
  }
}

void dispatchSerialCliRequest(SerialCliRequest &request) {
  switch (request.verb) {
    case SerialCliVerb::NONE:
      return;
    case SerialCliVerb::HELP:
      serialCliPrintHelp();
      return;
    case SerialCliVerb::HELLO:
      serialCliReply("how are you");
      return;
    case SerialCliVerb::REBOOT: {
      WebCommand command;
      command.type = WebCommandType::RESTART;
      serialCliQueueCommand(command, request.verb);
      return;
    }
    case SerialCliVerb::UNKNOWN:
    case SerialCliVerb::LINE_TOO_LONG:
    case SerialCliVerb::INVALID_ARGS:
      Serial.print("ERR ");
      Serial.println(request.error != nullptr ? request.error
                                              : "invalid arguments");
      return;
    case SerialCliVerb::FACTORY_RESET: {
      WebCommand command;
      command.type = WebCommandType::FACTORY_RESET;
      serialCliQueueIfSafe(command, request.verb);
      return;
    }
    case SerialCliVerb::RESET_DEVICE_PASSWORD: {
      WebCommand command;
      command.type = WebCommandType::RESET_DEVICE_PASSWORD;
      serialCliQueueIfSafe(command, request.verb);
      return;
    }
    case SerialCliVerb::SET_DEVICE_PASSWORD: {
      WebCommand command;
      command.setNetworkType(WebCommandType::CHANGE_DEVICE_PASSWORD);
      copyCString(command.network.password, sizeof(command.network.password), request.arg1);
      serialCliQueueIfSafe(command, request.verb);
      memset(request.arg1, 0, sizeof(request.arg1));
      return;
    }
    case SerialCliVerb::SET_WIFI: {
      WebCommand command;
      command.setNetworkType(WebCommandType::SAVE_NETWORK);
      copyCString(command.network.ssid, sizeof(command.network.ssid), request.arg1);
      copyCString(command.network.password, sizeof(command.network.password), request.arg2);
      command.network.openNetwork = request.openNetwork;
      command.network.commitConfirmed = true;
      serialCliQueueIfSafe(command, request.verb);
      memset(request.arg1, 0, sizeof(request.arg1));
      memset(request.arg2, 0, sizeof(request.arg2));
      return;
    }
    case SerialCliVerb::CLEAR_WIFI: {
      WebCommand command;
      command.type = WebCommandType::FORGET_NETWORK;
      serialCliQueueIfSafe(command, request.verb);
      return;
    }
    case SerialCliVerb::RESET_NETWORK_AP: {
      WebCommand command;
      command.type = WebCommandType::RESET_NETWORK_AP;
      serialCliQueueIfSafe(command, request.verb);
      return;
    }
    case SerialCliVerb::CLEAR_SHOTS: {
      if (!controlAllowsConfigurationNow()) {
        serialCliRejectUnsafe();
        return;
      }
      if (!clearShotLog()) {
        serialCliReply("ERR shot history clear failed");
        return;
      }
      serialCliReply("OK shots cleared");
      return;
    }
    case SerialCliVerb::SERIAL_DEBUG_ON:
    case SerialCliVerb::SERIAL_DEBUG_OFF: {
      if (!settingsPersistenceAvailable()) {
        serialCliReply("ERR settings persistence unavailable");
        return;
      }
      const bool enable = request.verb == SerialCliVerb::SERIAL_DEBUG_ON;
      if (!enable) {
        serialCliReply("OK serial debug off");
      }
      RuntimeConfig candidate = runtimeConfig;
      setSerialLogLevel(candidate, enable ? LogLevel::INFO : LogLevel::NONE);
      ++candidate.revision;
      if (candidate.revision == 0) {
        candidate.revision = 1;
      }
      commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_USER);
      if (enable) {
        serialCliReply("OK serial debug on");
      }
      return;
    }
    case SerialCliVerb::DEBUG_FULL:
      serialCliApplyDebugPersist(true, LogLevel::DEBUG, "OK debug full");
      return;
    case SerialCliVerb::DEBUG_OFF:
      serialCliApplyDebugPersist(false, LogLevel::NONE, "OK debug off");
      return;
    case SerialCliVerb::DEBUG_STATUS:
      serialCliPrintDebugStatus(serialLogLevel != LogLevel::NONE, serialLogLevel,
                                ringRetainLogLevel);
      return;
    case SerialCliVerb::WIFI_CONNECT:
      serialCliQueueNetworkAction(WebCommandType::WIFI_CONNECT, request.verb);
      return;
    case SerialCliVerb::WIFI_DISCONNECT:
      serialCliQueueNetworkAction(WebCommandType::WIFI_DISCONNECT,
                                  request.verb);
      return;
    case SerialCliVerb::WIFI_RESTART:
      serialCliQueueNetworkAction(WebCommandType::WIFI_RESTART, request.verb);
      return;
    case SerialCliVerb::AP_START:
      serialCliQueueNetworkAction(WebCommandType::AP_START, request.verb);
      return;
    case SerialCliVerb::AP_STOP:
      serialCliQueueNetworkAction(WebCommandType::AP_STOP, request.verb);
      return;
    case SerialCliVerb::WEBUI_START:
      serialCliQueueNetworkAction(WebCommandType::WEBUI_START, request.verb);
      return;
    case SerialCliVerb::WEBUI_STOP:
      serialCliQueueNetworkAction(WebCommandType::WEBUI_STOP, request.verb);
      return;
    case SerialCliVerb::WEBUI_RESTART:
      serialCliQueueNetworkAction(WebCommandType::WEBUI_RESTART, request.verb);
      return;
    case SerialCliVerb::WIFI_STATUS:
    case SerialCliVerb::AP_STATUS:
    case SerialCliVerb::WEBUI_STATUS:
    case SerialCliVerb::NET_STATUS:
      serialCliPrintLiveNetworkStatus(request.verb);
      return;
    case SerialCliVerb::LOG_DUMP:
      serialCliPrintLiveLogDump();
      return;
    case SerialCliVerb::HEALTH:
      serialCliPrintLiveHealth();
      return;
    case SerialCliVerb::SCALE_STATUS:
      serialCliPrintLiveScaleStatus();
      return;
    case SerialCliVerb::NTP_STATUS:
      serialCliPrintLiveNtpStatus();
      return;
    case SerialCliVerb::BLE_COMPAT_ENABLE:
    case SerialCliVerb::BLE_COMPAT_DISABLE: {
      WebCommand command;
      command.type = request.verb == SerialCliVerb::BLE_COMPAT_ENABLE
                         ? WebCommandType::BLE_COMPAT_ENABLE
                         : WebCommandType::BLE_COMPAT_DISABLE;
      serialCliQueueCommand(command, request.verb);
      return;
    }
    case SerialCliVerb::BLE_COMPAT_STATUS:
      serialCliPrintBleCompanionStatus();
      return;
  }
}

void serviceSerialCli() {
  size_t consumed = 0;
  while (Serial.available() > 0 &&
         consumed < SERIAL_CLI_MAX_BYTES_PER_LOOP) {
    const int incoming = Serial.read();
    ++consumed;
    if (incoming < 0) {
      break;
    }
    SerialCliRequest request;
    if (serialCliFeed(serialCliParser, static_cast<char>(incoming),
                      request)) {
      dispatchSerialCliRequest(request);
    }
  }
}

#ifndef SHOT_STOPPER_HOST_TEST
void serviceBootRecoverySafety() {
  serviceRelaySafety();
  localBuzzer.service(millis());
  if (!feedCurrentTaskWatchdog()) {
    reportTaskWatchdogFault();
    tripRelaySafety(RelaySafetyFault::TASK_WATCHDOG_FAILURE);
  }
  serviceSafetyHeartbeat(false);
}

void waitForRecoveryBuzzer() {
  while (localBuzzer.busy()) {
    serviceBootRecoverySafety();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void completeBootRecovery(RecoveryOperation operation);

void abandonFailedBootRecovery(RecoveryOperation operation) {
  (void)playRecoveryCue(BuzzerCue::RECOVERY_ERROR);
  waitForRecoveryBuzzer();
  if (operation == RecoveryOperation::FACTORY_RESET) {
    (void)resetPersistedSettingsToFactory(persistedSettings);
  } else if (operation == RecoveryOperation::NETWORK_ACCESS_RESET) {
    (void)resetPersistedNetworkAccess(persistedSettings);
  }
  (void)abandonRecoveryIntent();
}

void abandonMalformedRecoveryIntent() {
  (void)playRecoveryCue(BuzzerCue::RECOVERY_ERROR);
  waitForRecoveryBuzzer();
  (void)resetPersistedSettingsToFactory(persistedSettings);
  (void)abandonRecoveryIntent();
}

bool applyBootRecoveryOperation(RecoveryOperation operation) {
  if (operation == RecoveryOperation::NETWORK_ACCESS_RESET) {
    return resetPersistedNetworkAccess(persistedSettings);
  }
  if (operation != RecoveryOperation::FACTORY_RESET) {
    return false;
  }

  return resetAllDurableStoresForNetwork(persistedSettings);
}

void completeBootRecovery(RecoveryOperation operation) {
  // Do not rewrite a valid latch: a failed put on full NVS can tear it.
  (void)ensureRecoveryIntent(operation);
  if (!applyBootRecoveryOperation(operation)) {
    abandonFailedBootRecovery(operation);
    return;
  }
  (void)clearRecoveryIntent();

  const BuzzerCue success =
      operation == RecoveryOperation::FACTORY_RESET
          ? BuzzerCue::FACTORY_RESET_OK
          : BuzzerCue::NETWORK_RESET_OK;
  (void)playRecoveryCue(success);
  waitForRecoveryBuzzer();
  // Preserve the final 50 ms pause specified by the recovery cue before the
  // reset cuts power to the buzzer output.
  const uint32_t pauseStartedAtMs = millis();
  while (static_cast<uint32_t>(millis() - pauseStartedAtMs) <
         BUZZER_RECOVERY_PULSE_GAP_MS) {
    serviceBootRecoverySafety();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  // If the latch could not be dropped, continue this boot instead of
  // restarting into the same pending operation forever.
  if (!bootRecoveryShouldRestartAfterSuccess()) {
    return;
  }
  recordResetUptime(millis(), true);
  ESP.restart();
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void resumePendingBootRecovery() {
  RecoveryIntent intent;
  const PendingRecoveryKind kind = inspectPendingRecovery(intent);
  if (kind == PendingRecoveryKind::VALID) {
    completeBootRecovery(static_cast<RecoveryOperation>(intent.operation));
    return;
  }
  if (kind == PendingRecoveryKind::MALFORMED) {
    abandonMalformedRecoveryIntent();
  }
}

void maybeRunBootRecoveryGesture() {
  const bool powerOnReset = currentSafetyResetIsPowerOn();
  const bool switchStablyOn = powerOnReset && machineBootActivatorHeldStably();
  if (!recoveryGestureEntryAllowed(powerOnReset, switchStablyOn)) {
    return;
  }

  RecoveryGestureRecognizer recognizer;
  recognizer.begin(millis());
  (void)playRecoveryCue(BuzzerCue::RECOVERY_START);

  for (;;) {
    machineSampleInput();
    const MachineIntention intent = machinePollIntention();
    const RecoveryGestureResult result = recognizer.update(
        millis(), intent.holdActive, intent.turnedOn, intent.turnedOff);
    if (result == RecoveryGestureResult::NETWORK_ACCESS_RESET) {
      completeBootRecovery(RecoveryOperation::NETWORK_ACCESS_RESET);
      return;
    }
    if (result == RecoveryGestureResult::FACTORY_RESET) {
      completeBootRecovery(RecoveryOperation::FACTORY_RESET);
      return;
    }
    if (result == RecoveryGestureResult::TIMED_OUT) {
      (void)playRecoveryCue(BuzzerCue::RECOVERY_START);
      waitForRecoveryBuzzer();
      return;
    }
    serviceBootRecoverySafety();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
#endif

// ---------------------------------------------------------------------------
