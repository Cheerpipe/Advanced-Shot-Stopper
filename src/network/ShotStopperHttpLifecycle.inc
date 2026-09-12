void ShotStopperNetwork::processAcceptedCommands() {
  if (!acceptedCommandQueue_) {
    return;
  }

  if (completionPending_) {
    if (!callbacks_.enqueueWebCommand(completionCommand_)) {
      return;
    }
    completionPending_ = false;
    completionCommand_ = WebCommand{};
  }

  const uint32_t now = millis();
  if (!acceptedCommandPending_) {
    if (xQueueReceive(acceptedCommandQueue_.get(), &acceptedCommand_, 0) !=
        pdTRUE) {
      return;
    }
    acceptedCommandPending_ = true;
    acceptedCommandAttempts_ = 0;
    acceptedCommandReceivedAtMs_ = now;
    acceptedCommandRetryAtMs_ = now;
  }
  if (static_cast<int32_t>(now - acceptedCommandRetryAtMs_) < 0) {
    return;
  }

  if (isCliNetworkAction(acceptedCommand_.type)) {
    recordCommandResult(acceptedCommand_.requestId,
                        CommandResultState::RESERVED);
    ++acceptedCommandAttempts_;
    if (!processAcceptedCommand(acceptedCommand_)) {
      if (acceptedCommandAttempts_ < COMMAND_MAX_ATTEMPTS) {
        acceptedCommandRetryAtMs_ =
            now + COMMAND_RETRY_MIN_MS * acceptedCommandAttempts_;
        log(DebugCategory::SECURITY, DebugCode::COMMAND_RETRY,
            acceptedCommand_.requestId, acceptedCommandAttempts_);
        return;
      }
      recordCommandResult(acceptedCommand_.requestId,
                          CommandResultState::FAILED);
      actionLogf("ERR %s failed after retries",
                 webCommandTypeName(acceptedCommand_.type));
      acceptedCommandPending_ = false;
      acceptedCommand_ = WebCommand{};
      return;
    }
    recordCommandResult(acceptedCommand_.requestId,
                        CommandResultState::APPLIED);
    noteCliNetworkProgress();
    acceptedCommandPending_ = false;
    acceptedCommand_ = WebCommand{};
    return;
  }

  if (acceptedCommand_.type == WebCommandType::MAINTENANCE_COMPLETE &&
      acceptedCommand_.maintenanceLeaseId == 0) {
    const CommandResultState terminalState =
        acceptedCommand_.resultState == CommandResultState::APPLIED ||
                acceptedCommand_.resultState == CommandResultState::PERSISTED ||
                acceptedCommand_.resultState == CommandResultState::FAILED ||
                acceptedCommand_.resultState == CommandResultState::CANCELED
            ? acceptedCommand_.resultState
            : (acceptedCommand_.succeeded ? CommandResultState::APPLIED
                                          : CommandResultState::CANCELED);
    recordCommandResult(
        acceptedCommand_.requestId, terminalState);
    acceptedCommandPending_ = false;
    acceptedCommand_ = WebCommand{};
    return;
  }

  // Maintenance commands used to keep a ControlStatusSnapshot in this
  // frame. Gate fields live in ControlGateSnapshot so WEBUI_RESTART does not
  // carry that object into httpd_stop() / LwIP.
  processAcceptedMaintenanceCommand(now);
}

void ShotStopperNetwork::processAcceptedMaintenanceCommand(uint32_t now) {
  const ControlGateSnapshot control = controlGate();
  const bool matchingLease =
      acceptedCommand_.maintenanceLeaseId != 0 &&
      control.maintenanceLeaseActive &&
      control.maintenanceLeaseId == acceptedCommand_.maintenanceLeaseId;
  if (!matchingLease) {
    // The control task publishes the lease after enqueueing the command. Wait
    // for that publication, but reject a lease that was canceled/replaced.
    const bool publicationTimedOut =
        static_cast<uint32_t>(now - acceptedCommandReceivedAtMs_) >=
        MAINTENANCE_PUBLICATION_TIMEOUT_MS;
    if (control.maintenanceLeaseActive || control.activeCycle ||
        control.physicalActivatorOn || publicationTimedOut) {
      if (acceptedCommand_.type == WebCommandType::SAVE_WEBHOOK) {
        clearStagedWebhook(acceptedCommand_.webhookStageRequestId);
      }
      (void)enqueueMaintenanceCompletion(
          acceptedCommand_, false, CommandResultState::CANCELED);
      acceptedCommandPending_ = false;
      acceptedCommand_ = WebCommand{};
    }
    return;
  }
  if (control.activeCycle || control.relayClosed ||
      control.physicalActivatorOn) {
    if (acceptedCommand_.type == WebCommandType::SAVE_WEBHOOK) {
      clearStagedWebhook(acceptedCommand_.webhookStageRequestId);
    }
    (void)enqueueMaintenanceCompletion(
        acceptedCommand_, false, CommandResultState::CANCELED);
    acceptedCommandPending_ = false;
    acceptedCommand_ = WebCommand{};
    return;
  }

  recordCommandResult(acceptedCommand_.requestId,
                      CommandResultState::RESERVED);
  ++acceptedCommandAttempts_;
  if (!processAcceptedCommand(acceptedCommand_)) {
    if (acceptedCommandAttempts_ < COMMAND_MAX_ATTEMPTS) {
      const uint32_t backoff =
          COMMAND_RETRY_MIN_MS * acceptedCommandAttempts_;
      acceptedCommandRetryAtMs_ = now + backoff;
      log(DebugCategory::SECURITY, DebugCode::COMMAND_RETRY,
          acceptedCommand_.requestId, acceptedCommandAttempts_);
      return;
    }
    if (acceptedCommand_.type == WebCommandType::SAVE_WEBHOOK) {
      clearStagedWebhook(acceptedCommand_.webhookStageRequestId);
    }
    (void)enqueueMaintenanceCompletion(acceptedCommand_, false);
    log(DebugCategory::SECURITY, DebugCode::COMMAND_FAILED,
        acceptedCommand_.requestId, acceptedCommandAttempts_);
    acceptedCommandPending_ = false;
    acceptedCommand_ = WebCommand{};
    return;
  }

  if (acceptedCommand_.type != WebCommandType::START_WIFI_SCAN) {
    (void)enqueueMaintenanceCompletion(acceptedCommand_, true);
  }
  acceptedCommandPending_ = false;
  acceptedCommand_ = WebCommand{};
}

bool ShotStopperNetwork::enqueueMaintenanceCompletion(
    const WebCommand &command, bool succeeded,
    CommandResultState failureState) {
  WebCommand result;
  result.type = WebCommandType::MAINTENANCE_COMPLETE;
  result.requestId = command.requestId;
  result.maintenanceLeaseId = command.maintenanceLeaseId;
  result.succeeded = succeeded;
  recordCommandResult(
      command.requestId,
      succeeded ? (command.type == WebCommandType::PERSIST_RUNTIME ||
                           command.type == WebCommandType::SAVE_NETWORK ||
                           command.type == WebCommandType::SAVE_WEBHOOK ||
                           command.type == WebCommandType::FORGET_NETWORK ||
                           command.type == WebCommandType::CHANGE_DEVICE_PASSWORD ||
                           command.type == WebCommandType::RESET_DEVICE_PASSWORD ||
                           command.type == WebCommandType::RESET_NETWORK_AP ||
                           command.type == WebCommandType::FACTORY_RESET ||
                           command.type == WebCommandType::RESTART
                       ? CommandResultState::PERSISTED
                       : CommandResultState::APPLIED)
                : failureState);
  if (callbacks_.enqueueWebCommand(result)) {
    return true;
  }
  completionCommand_ = result;
  completionPending_ = true;
  return false;
}

bool ShotStopperNetwork::processAcceptedCommand(const WebCommand &command) {
  if (isCliNetworkAction(command.type)) {
    return handleCliNetworkAction(command, millis());
  }
  if (command.type == WebCommandType::START_WIFI_SCAN) {
    dataMux_.lock();
    g_wifiScan = WifiScanSnapshot{};
    g_wifiScan.state = WifiScanState::QUEUED;
    g_wifiScan.updatedAtMs = millis();
    scanRequested_ = true;
    scanMaintenanceLeaseId_ = command.maintenanceLeaseId;
    scanRequestId_ = command.requestId;
    dataMux_.unlock();
    return true;
  }
  return processPersistedCommand(command);
}

bool ShotStopperNetwork::processPersistedCommand(const WebCommand &command) {
  PersistedSettings next = settingsCopy();
  bool persist = false;
  bool factoryReset = false;
  bool applyWifiPsAfterPersist = false;
  switch (command.type) {
    case WebCommandType::PERSIST_RUNTIME:
      if (command.persistPresets && callbacks_.copyPresetBank != nullptr) {
        callbacks_.copyPresetBank(&next.presets);
      }
      {
        const RuntimeConfig composed =
            composeEffectiveConfig(command.config, next.presets);
        if (validateRuntimeConfig(composed) != ConfigValidationError::NONE) {
          log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
          return false;
        }
      }
      next.runtime = command.config;
      persist = true;
      break;

    case WebCommandType::SAVE_WEBHOOK: {
      WebhookConfig staged = {};
      dataMux_.lock();
      const bool matches = stagedWebhookRequestId_ != 0 &&
                           stagedWebhookRequestId_ == command.webhookStageRequestId;
      if (matches) staged = stagedWebhook_;
      dataMux_.unlock();
      if (!matches || !validWebhookConfig(staged)) {
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return false;
      }
      next.webhook = staged;
      persist = true;
      break;
    }

    case WebCommandType::SAVE_NETWORK: {
      char password[WIFI_PASSWORD_CAPACITY] = {};
      const bool reusePassword = shouldReuseSavedWifiCredentials(
          command.network.ssid, command.network.password, command.network.openNetwork,
          next.staConfigured, next.staSsid, next.staOpen);
      copyCString(password, sizeof(password),
                  reusePassword ? next.staPassword : command.network.password);
      if (!validWifiSsid(command.network.ssid) ||
          !validWifiPassword(password, command.network.openNetwork) ||
          !validStaAddressConfig(command.network.staIpMode, command.network.staIp,
                                 command.network.staNetmask, command.network.staGateway,
                                 command.network.staDns1, command.network.staDns2)) {
        memset(password, 0, sizeof(password));
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return false;
      }
      const bool sameCredentials =
          next.staConfigured && reusePassword &&
          strcmp(next.staSsid, command.network.ssid) == 0 &&
          next.staOpen == command.network.openNetwork &&
          next.staIpMode == command.network.staIpMode &&
          memcmp(next.staIp, command.network.staIp, sizeof(next.staIp)) == 0 &&
          memcmp(next.staNetmask, command.network.staNetmask,
                 sizeof(next.staNetmask)) == 0 &&
          memcmp(next.staGateway, command.network.staGateway,
                 sizeof(next.staGateway)) == 0 &&
          memcmp(next.staDns1, command.network.staDns1, sizeof(next.staDns1)) == 0 &&
          memcmp(next.staDns2, command.network.staDns2, sizeof(next.staDns2)) == 0;
      // Specified sleep + identical STA: persist sleep if it changed,
      // otherwise no-op. Never restart (second Save must not go PENDING).
      if (command.network.wifiSleepSpecified && sameCredentials) {
        memset(password, 0, sizeof(password));
        if (next.staWifiSleep != command.network.wifiSleep) {
          next.staWifiSleep = command.network.wifiSleep;
          persist = true;
          applyWifiPsAfterPersist = true;
        }
        break;
      }
      if (!command.network.commitConfirmed && next.staConfigured &&
          next.staConfigState ==
              static_cast<uint8_t>(StaConfigState::CONFIRMED)) {
        copyActiveStaToLkg(next);
      }
      next.staConfigured = true;
      next.staOpen = command.network.openNetwork;
      if (command.network.wifiSleepSpecified) {
        next.staWifiSleep = command.network.wifiSleep;
      }
      memset(next.staSsid, 0, sizeof(next.staSsid));
      memset(next.staPassword, 0, sizeof(next.staPassword));
      copyCString(next.staSsid, sizeof(next.staSsid), command.network.ssid);
      copyCString(next.staPassword, sizeof(next.staPassword), password);
      memset(password, 0, sizeof(password));
      next.staIpMode = command.network.staIpMode;
      memcpy(next.staIp, command.network.staIp, sizeof(next.staIp));
      memcpy(next.staNetmask, command.network.staNetmask, sizeof(next.staNetmask));
      memcpy(next.staGateway, command.network.staGateway, sizeof(next.staGateway));
      memcpy(next.staDns1, command.network.staDns1, sizeof(next.staDns1));
      memcpy(next.staDns2, command.network.staDns2, sizeof(next.staDns2));
      finalizeSavedStaCredentials(next, command.network.commitConfirmed);
      persist = true;
      restartPending_ = true;
      break;
    }

    case WebCommandType::FORGET_NETWORK:
      clearStaNetwork(next);
      persist = true;
      restartPending_ = true;
      break;

    case WebCommandType::CHANGE_DEVICE_PASSWORD:
      if (!setDevicePassword(next, command.network.password)) {
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return false;
      }
      persist = true;
      apRestartPending_ = snapshot().apActive;
      break;

    case WebCommandType::RESET_DEVICE_PASSWORD:
      if (!initializeDefaultDevicePassword(next)) {
        return false;
      }
      persist = true;
      apRestartPending_ = snapshot().apActive;
      log(DebugCategory::SECURITY, DebugCode::DEVICE_PASSWORD_RESET);
      break;

    case WebCommandType::RESET_NETWORK_AP:
      clearStaNetwork(next);
      if (!initializeDefaultDevicePassword(next)) {
        return false;
      }
      persist = true;
      restartPending_ = true;
      log(DebugCategory::SECURITY, DebugCode::NETWORK_RESET);
      break;

    case WebCommandType::FACTORY_RESET:
      if (callbacks_.resetAllDurableStores == nullptr ||
          callbacks_.releaseNvsSpaceForFactoryReset == nullptr) {
        return false;
      }
      if (!ensureFactoryResetIntent(
              [&]() { return callbacks_.releaseNvsSpaceForFactoryReset(); })) {
        return false;
      }
      if (!callbacks_.resetAllDurableStores(next)) {
        if (!otaRollbackRestartPending_) {
          apRestartPending_ = false;
        }
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return false;
      }
      (void)clearRecoveryIntent();
      factoryReset = true;
      restartPending_ = true;
      log(DebugCategory::SECURITY, DebugCode::FACTORY_RESET);
      break;

    case WebCommandType::RESTART:
      persist = true;
      restartPending_ = true;
      log(DebugCategory::SECURITY, DebugCode::RESTART_REQUESTED);
      break;

    case WebCommandType::WIFI_CONNECT:
    case WebCommandType::WIFI_DISCONNECT:
    case WebCommandType::WIFI_RESTART:
    case WebCommandType::AP_START:
    case WebCommandType::AP_STOP:
    case WebCommandType::WEBUI_START:
    case WebCommandType::WEBUI_STOP:
    case WebCommandType::WEBUI_RESTART:
    case WebCommandType::START_WIFI_SCAN:
      return false;

    default:
      return false;
  }

  if (persist) {
    overlayLiveShotSettings(next);
    if (!savePersistedSettings(next)) {
      const bool restartAnyway = command.type == WebCommandType::RESTART;
      if (!otaRollbackRestartPending_ && !restartAnyway) {
        restartPending_ = false;
      }
      apRestartPending_ = false;
      if (restartAnyway) {
        // A broken/full NVS must not strand a committed OTA image or make
        // restart unavailable as a recovery action. Live unsaved changes may
        // be lost, but the last verified durable settings remain intact.
        restartRequestedAtMs_ = millis();
        return true;
      }
      // NVS write failure is not a config validation reject. PERSIST_RUNTIME
      // failures are reported clearly when the maintenance lease completes.
      if (command.type != WebCommandType::PERSIST_RUNTIME) {
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
      }
      return false;
    }
    dataMux_.lock();
    settings_ = next;
    status_.wifiConfigured = next.staConfigured;
    status_.staConfigState = next.staConfigState;
    dataMux_.unlock();
    if (command.type == WebCommandType::SAVE_WEBHOOK) {
      webhooks_.setConfig(next.webhook);
      clearStagedWebhook(command.webhookStageRequestId);
    }
    publishConfiguredAddressStatus();
    log(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
        static_cast<int32_t>(next.runtime.revision));
  } else if (factoryReset) {
    dataMux_.lock();
    settings_ = next;
    status_.wifiConfigured = false;
    status_.staConfigState =
        static_cast<uint8_t>(StaConfigState::CONFIRMED);
    dataMux_.unlock();
    publishConfiguredAddressStatus();
    log(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
        static_cast<int32_t>(next.runtime.revision));
  }

  if (restartPending_ || apRestartPending_) {
    restartRequestedAtMs_ = millis();
  }
  if (applyWifiPsAfterPersist) {
    applyWifiPowerSave();
  }
  return true;
}

bool ShotStopperNetwork::handleCliNetworkAction(const WebCommand &command,
                                                uint32_t now) {
  refreshExtendedStatus(now);
  switch (command.type) {
    case WebCommandType::WIFI_CONNECT:
    case WebCommandType::WIFI_DISCONNECT:
    case WebCommandType::WIFI_RESTART:
      return handleCliWifiAction(command, now);
    case WebCommandType::AP_START:
    case WebCommandType::AP_STOP:
      return handleCliApAction(command, now);
    case WebCommandType::WEBUI_START:
    case WebCommandType::WEBUI_STOP:
    case WebCommandType::WEBUI_RESTART:
      return handleCliWebUiAction(command, now);
    default:
      return false;
  }
}

bool ShotStopperNetwork::handleCliWifiAction(const WebCommand &command,
                                             uint32_t now) {
  switch (command.type) {
    case WebCommandType::WIFI_CONNECT: {
      const NetworkSettingsSnapshot settings = networkSettingsSnapshot();
      if (!settings.staConfigured) {
        actionLog("ERR no STA configured; use SET_WIFI");
        printActionSnapshot("WIFI_CONNECT", false);
        return true;
      }
      const NetworkStatusSnapshot status = snapshot();
      if (status.staState == StaState::CONNECTED &&
          WiFi.status() == WL_CONNECTED) {
        actionLog("OK already connected");
        printActionSnapshot("WIFI_CONNECT", true);
        return true;
      }
      staReconnectHeld_ = false;
      if (!beginStationConnect(settings, now)) {
        actionLog("WIFI_CONNECT deferred; brew RF active");
        printActionSnapshot("WIFI_CONNECT", false);
        return true;
      }
      actionLog("WIFI_CONNECT associating saved STA");
      printActionSnapshot("WIFI_CONNECT", true);
      return true;
    }
    case WebCommandType::WIFI_DISCONNECT: {
      staReconnectHeld_ = true;
      WiFi.disconnect(false, false);
      stopNtp();
      staNtpEligibleAtMs_ = 0;
      g_wallClock.markDisabled();
      dataMux_.lock();
      status_.staState = status_.wifiConfigured ? StaState::DISCONNECTED
                                                : StaState::NOT_CONFIGURED;
      status_.staIp[0] = '\0';
      clearStaLinkMetrics();
      dataMux_.unlock();
      actionLog("WIFI_DISCONNECT STA down; reconnect held");
      printActionSnapshot("WIFI_DISCONNECT", true);
      return true;
    }
    case WebCommandType::WIFI_RESTART: {
      const NetworkSettingsSnapshot settings = networkSettingsSnapshot();
      if (!settings.staConfigured) {
        actionLog("ERR no STA configured; use SET_WIFI");
        printActionSnapshot("WIFI_RESTART", false);
        return true;
      }
      staReconnectHeld_ = false;
      stopNtp();
      staNtpEligibleAtMs_ = 0;
      g_wallClock.markDisabled();
      dataMux_.lock();
      status_.staState = StaState::DISCONNECTED;
      status_.staIp[0] = '\0';
      clearStaLinkMetrics();
      dataMux_.unlock();
      if (!beginStationConnect(settings, now)) {
        actionLog("WIFI_RESTART deferred; brew RF active");
        printActionSnapshot("WIFI_RESTART", false);
        return true;
      }
      actionLog("WIFI_RESTART reconnecting saved STA");
      printActionSnapshot("WIFI_RESTART", true);
      return true;
    }
    default:
      return false;
  }
}

bool ShotStopperNetwork::handleCliApAction(const WebCommand &command,
                                           uint32_t now) {
  switch (command.type) {
    case WebCommandType::AP_START: {
      apStartHeld_ = false;
      apKeepRequested_ = true;
      const bool ok = ensureAccessPoint(now, true);
      if (!ok) {
        apKeepRequested_ = false;
      }
      actionLog(ok ? "AP_START SoftAP requested" : "ERR AP_START failed");
      printActionSnapshot("AP_START", ok);
      return true;
    }
    case WebCommandType::AP_STOP: {
      apStartHeld_ = true;
      apKeepRequested_ = false;
      const bool staUp = snapshot().staState == StaState::CONNECTED &&
                         WiFi.status() == WL_CONNECTED;
      if (staUp) {
        stopSoftApLeaveHttp();
      } else {
        stopSoftAp(true);
      }
      actionLog("AP_STOP SoftAP down; auto-raise held");
      printActionSnapshot("AP_STOP", true);
      return true;
    }
    default:
      return false;
  }
}

bool ShotStopperNetwork::handleCliWebUiAction(const WebCommand &command,
                                              uint32_t now) {
  (void)now;
  switch (command.type) {
    case WebCommandType::WEBUI_START: {
      httpStartHeld_ = false;
      if (server_ != nullptr) {
        actionLog("OK already running");
        printActionSnapshot("WEBUI_START", true);
        return true;
      }
      const bool ok = startHttpServer();
      dataMux_.lock();
      status_.networkActive = ok && (status_.apActive ||
                                     status_.staState == StaState::CONNECTED);
      status_.httpActive = ok;
      dataMux_.unlock();
      actionLog(ok ? "WEBUI_START httpd up" : "ERR WEBUI_START httpd_start failed");
      if (!ok) {
        actionLogf("WEBUI_START heap hint free=%u",
                   static_cast<unsigned>(ESP.getFreeHeap()));
      }
      printActionSnapshot("WEBUI_START", ok);
      return true;
    }
    case WebCommandType::WEBUI_STOP: {
      httpStartHeld_ = true;
      stopHttpServer();
      dataMux_.lock();
      status_.networkActive = false;
      status_.httpActive = false;
      dataMux_.unlock();
      actionLog("WEBUI_STOP httpd down; auto-start held");
      printActionSnapshot("WEBUI_STOP", true);
      return true;
    }
    case WebCommandType::WEBUI_RESTART: {
      httpStartHeld_ = false;
      stopHttpServer();
      const bool ok = startHttpServer();
      dataMux_.lock();
      status_.networkActive = ok && (status_.apActive ||
                                     status_.staState == StaState::CONNECTED);
      status_.httpActive = ok;
      dataMux_.unlock();
      actionLog(ok ? "WEBUI_RESTART httpd up" : "ERR WEBUI_RESTART failed");
      if (!ok) {
        actionLogf("WEBUI_RESTART heap hint free=%u",
                   static_cast<unsigned>(ESP.getFreeHeap()));
      }
      printActionSnapshot("WEBUI_RESTART", ok);
      return true;
    }
    default:
      return false;
  }
}

void ShotStopperNetwork::log(DebugCategory category, DebugCode code,
                             int32_t argument1, int32_t argument2) {
  if (callbacks_.addDebugEvent != nullptr) {
    callbacks_.addDebugEvent(category, code, argument1, argument2);
  }
}

void ShotStopperNetwork::actionLog(const char *message) {
  if (message != nullptr) {
    ::serialTraceCategory(networkTextLogLevel(message), DebugCategory::NETWORK,
                          message);
  }
}

void ShotStopperNetwork::actionLogf(const char *fmt, ...) {
  if (fmt == nullptr) {
    return;
  }
  char line[DEBUG_EVENT_TEXT_CAPACITY] = {};
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  ::serialTraceCategory(networkTextLogLevel(line), DebugCategory::NETWORK,
                        line);
}

void ShotStopperNetwork::lifecycleLog(const char *message) {
  if (message != nullptr) {
    ::serialTraceCategory(networkTextLogLevel(message), DebugCategory::NETWORK,
                          message);
  }
}

void ShotStopperNetwork::lifecycleLogf(const char *fmt, ...) {
  if (fmt == nullptr) {
    return;
  }
  char line[DEBUG_EVENT_TEXT_CAPACITY] = {};
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  ::serialTraceCategory(networkTextLogLevel(line), DebugCategory::NETWORK,
                        line);
}

void ShotStopperNetwork::printActionSnapshot(const char *command, bool ok) {
  refreshExtendedStatus(millis());
  const NetworkStatusSnapshot status = snapshot();
  actionLogf("%s %s ssid=%s mode=%s wifiStatus=%ld %s sta=%s ip=%s rssi=%d "
             "ap=%s apIp=%s clients=%u http=%s holds sta/ap/http=%d/%d/%d",
             command != nullptr ? command : "NETWORK",
             ok ? "OK" : "ERR",
             status.staSsid[0] != '\0' ? status.staSsid : "-",
             serialCliWifiModeName(status.wifiMode),
             static_cast<long>(status.wifiStatus),
             serialCliWlStatusName(status.wifiStatus),
             staStateName(status.staState),
             status.staIp[0] != '\0' ? status.staIp : "-",
             status.staLinkMetricsValid ? static_cast<int>(status.staRssi) : 0,
             status.apActive ? "up" : "down",
             status.apIp,
             static_cast<unsigned>(status.apClients),
             status.httpActive ? "up" : "down",
             status.staReconnectHeld ? 1 : 0,
             status.apStartHeld ? 1 : 0,
             status.httpStartHeld ? 1 : 0);
}

void ShotStopperNetwork::noteCliNetworkProgress() {
  if (startupComplete_) {
    return;
  }
  const NetworkStatusSnapshot status = snapshot();
  if (!status.apActive && server_ == nullptr &&
      status.staState != StaState::CONNECTING &&
      status.staState != StaState::CONNECTED) {
    return;
  }
  startupComplete_ = true;
  startupFailures_ = 0;
  networkRetryAtMs_ = 0;
  dataMux_.lock();
  status_.startupFailures = 0;
  dataMux_.unlock();
}

void ShotStopperNetwork::refreshExtendedStatus(uint32_t now) {
  const TimeStatusSnapshot timeStatus = g_wallClock.snapshot(now);
  const wl_status_t wifiStatus = WiFi.status();
  const bool staConnected = wifiStatus == WL_CONNECTED;
  const uint8_t wifiMode = static_cast<uint8_t>(WiFi.getMode());
  const uint8_t channel = static_cast<uint8_t>(WiFi.channel());
  WifiPsLive wifiPs = WifiPsLive::UNKNOWN;
  if (wifiMode != static_cast<uint8_t>(WIFI_OFF)) {
    wifi_ps_type_t ps = WIFI_PS_NONE;
    if (esp_wifi_get_ps(&ps) == ESP_OK) {
      switch (ps) {
        case WIFI_PS_NONE:
          wifiPs = WifiPsLive::NONE;
          break;
        case WIFI_PS_MIN_MODEM:
          wifiPs = WifiPsLive::MIN_MODEM;
          break;
        case WIFI_PS_MAX_MODEM:
          wifiPs = WifiPsLive::MAX_MODEM;
          break;
        default:
          wifiPs = WifiPsLive::UNKNOWN;
          break;
      }
    }
  }
  char staMac[18] = {};
  char staBssid[18] = {};
  char apMac[18] = {};
  uint8_t mac[6] = {};
  if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
    formatWifiMac(mac, staMac, sizeof(staMac));
  }
  if (staConnected) {
    const uint8_t *bssid = WiFi.BSSID();
    if (bssid != nullptr) {
      formatWifiMac(bssid, staBssid, sizeof(staBssid));
    }
  }
  if (esp_wifi_get_mac(WIFI_IF_AP, mac) == ESP_OK) {
    formatWifiMac(mac, apMac, sizeof(apMac));
  }
  const bool ntpArm = ntpMayArm(now, staConnected);
  const bool httpActive = server_ != nullptr;
  const bool factoryPassword = passwordIsFactoryDefault(settings_);
  dataMux_.lock();
  status_.httpActive = httpActive;
  status_.wifiMode = wifiMode;
  status_.channel = channel;
  status_.wifiPs = wifiPs;
  status_.wifiCoex = snapshotRfCoexPreference();
  status_.wifiStatus = static_cast<int32_t>(wifiStatus);
  status_.staReconnectHeld = staReconnectHeld_;
  status_.apStartHeld = apStartHeld_;
  status_.httpStartHeld = httpStartHeld_;
  status_.devicePasswordFactory = factoryPassword;
  status_.scanState = static_cast<uint8_t>(g_wifiScan.state);
  status_.ntpState = static_cast<uint8_t>(timeStatus.state);
  status_.ntpMayArm = ntpArm;
  status_.staConnectAgeMs =
      staConnectStartedAtMs_ != 0
          ? static_cast<uint32_t>(now - staConnectStartedAtMs_)
          : 0;
  status_.staReconnectAgeMs =
      staReconnectAttemptAtMs_ != 0
          ? static_cast<uint32_t>(now - staReconnectAttemptAtMs_)
          : 0;
  memset(status_.staMac, 0, sizeof(status_.staMac));
  memset(status_.staBssid, 0, sizeof(status_.staBssid));
  memset(status_.apMac, 0, sizeof(status_.apMac));
  copyCString(status_.staMac, sizeof(status_.staMac), staMac);
  copyCString(status_.staBssid, sizeof(status_.staBssid), staBssid);
  copyCString(status_.apMac, sizeof(status_.apMac), apMac);
  memset(status_.ntpActiveServer, 0, sizeof(status_.ntpActiveServer));
  copyCString(status_.ntpActiveServer, sizeof(status_.ntpActiveServer),
              timeStatus.activeServer);
  dataMux_.unlock();
}

void ShotStopperNetwork::ntpSyncNotificationCallback(struct timeval *tv) {
  ShotStopperNetwork *self = instance_;
  if (tv == nullptr || self == nullptr ||
      !self->ntpCallbackAccepting_.load(std::memory_order_acquire)) {
    return;
  }
  g_wallClock.queueSyncFromCallback(static_cast<uint32_t>(tv->tv_sec));
  if (self->taskHandle_ != nullptr) {
    xTaskNotifyGive(self->taskHandle_);
  }
}

void ShotStopperNetwork::stopNtp() {
  // Close the callback gate before stopping lwIP so a late completion from an
  // aborted attempt cannot be accepted after a shot/scale gate transition.
  ntpCallbackAccepting_.store(false, std::memory_order_release);
  if (ntpStarted_ || esp_sntp_enabled()) {
    esp_sntp_stop();
    ntpStarted_ = false;
  }
}

bool ShotStopperNetwork::ntpMayArm(uint32_t now, bool staConnected) const {
  if (!staConnected || server_ == nullptr) {
    return false;
  }
  if (brewRfActive()) {
    return false;
  }
  if (scaleConnecting_.load(std::memory_order_relaxed)) {
    return false;
  }
  if (staNtpEligibleAtMs_ != 0 &&
      static_cast<int32_t>(now - staNtpEligibleAtMs_) < 0) {
    return false;
  }
  return true;
}

void ShotStopperNetwork::abortNtpForRfGate() {
  const bool gateActive =
      brewRfActive() || scaleConnecting_.load(std::memory_order_acquire);
  const bool abortRequested =
      ntpAbortRequested_.exchange(false, std::memory_order_acq_rel);
  if (!gateActive && !abortRequested) {
    return;
  }
  const TimeStatusSnapshot timeStatus = g_wallClock.snapshot(millis());
  if (ntpStarted_ || timeStatus.state == TimeSyncState::SYNCING) {
    stopNtp();
    g_wallClock.cancelSyncing();
    ntpRearmPending_ = true;
  } else {
    g_wallClock.cancelPendingSync();
  }
}

bool ShotStopperNetwork::armNtp(uint32_t now, bool staConnected,
                                uint32_t expectedGateGeneration) {
  auto gateStable = [&]() {
    return rfGateGeneration_.load(std::memory_order_acquire) ==
               expectedGateGeneration &&
           ntpMayArm(millis(), staConnected);
  };
  if (!gateStable()) return false;

  // One locked snapshot of the NTP runtime fields: the control task rewrites
  // settings_.runtime wholesale under dataMux_, so reading it unlocked here
  // would race a torn copy.
  uint8_t ntpServerPreset = 0;
  char ntpServerCustom[NTP_SERVER_HOST_CAPACITY] = {};
  dataMux_.lock();
  ntpServerPreset = settings_.runtime.ntpServerPreset;
  memcpy(ntpServerCustom, settings_.runtime.ntpServerCustom,
         sizeof(ntpServerCustom));
  dataMux_.unlock();
  resolveNtpServerHost(ntpServerPreset, ntpServerCustom, ntpFailoverIndex_,
                       ntpServerBuffer_);
  g_wallClock.setSyncing(ntpServerBuffer_, now);
  ntpSyncStartedAtMs_ = now;
  esp_sntp_set_time_sync_notification_cb(ntpSyncNotificationCallback);
  ntpCallbackAccepting_.store(true, std::memory_order_release);

  // Close the check/use window before touching lwIP. A gate transition also
  // closes callback acceptance synchronously in its lock-free setter.
  if (!gateStable()) {
    stopNtp();
    g_wallClock.cancelSyncing();
    return false;
  }

  if (esp_sntp_enabled()) {
    esp_sntp_setservername(0, ntpServerBuffer_);
    sntp_restart();
    ntpStarted_ = true;
  } else {
    configTime(0, 0, ntpServerBuffer_);
    ntpStarted_ = true;
  }

  // Control/scale never waits for stopNtp. If it won the race with the actual
  // arm, detect its generation here and tear SNTP down on this network task.
  if (!gateStable()) {
    stopNtp();
    g_wallClock.cancelSyncing();
    return false;
  }
  return true;
}

void ShotStopperNetwork::handleNtpFailure(uint32_t now) {
  stopNtp();
  if (ntpFailoverIndex_ < 3) {
    ++ntpFailoverIndex_;
  }
  g_wallClock.markFailed(now, NTP_MAX_CONSECUTIVE_FAILURES);
  log(DebugCategory::NETWORK, DebugCode::TIME_SYNC_FAIL, ntpFailoverIndex_, 0);
}

void ShotStopperNetwork::serviceNtp(uint32_t now, bool staConnected) {
  dataMux_.lock();
  const uint32_t runtimeRevision = settings_.runtime.revision;
  dataMux_.unlock();
  if (runtimeRevision != ntpConfigRevision_) {
    ntpConfigRevision_ = runtimeRevision;
    ntpRearmPending_ = true;
  }

  if (!staConnected) {
    stopNtp();
    staNtpEligibleAtMs_ = 0;
    g_wallClock.markDisabled();
    return;
  }

  // Brew RF or scale connecting: do not arm, and abort any in-flight SNTP
  // without counting a failure so NTP rearms when the gate clears. This gate
  // precedes callback application: a completion racing with cancellation is
  // discarded rather than publishing a sync during critical activity.
  TimeStatusSnapshot timeStatus = g_wallClock.snapshot(now);
  const uint32_t gateGeneration =
      rfGateGeneration_.load(std::memory_order_acquire);
  if (!ntpMayArm(now, staConnected)) {
    if (ntpStarted_ || timeStatus.state == TimeSyncState::SYNCING) {
      stopNtp();
      g_wallClock.cancelSyncing();
      ntpRearmPending_ = true;
    } else {
      // Clears a callback that arrived just after a prior stop/cancel pass.
      g_wallClock.cancelPendingSync();
    }
    return;
  }

  if (g_wallClock.applyPendingSync(now)) {
    // SNTP is one-shot at the application level. Leaving it enabled would let
    // lwIP resync periodically without consulting the shot/BLE gate.
    stopNtp();
    ntpFailoverIndex_ = 0;
    log(DebugCategory::NETWORK, DebugCode::TIME_SYNC_OK);
    return;
  }

  timeStatus = g_wallClock.snapshot(now);

  if (timeStatus.state == TimeSyncState::SYNCING) {
    if (static_cast<uint32_t>(now - ntpSyncStartedAtMs_) >=
        NTP_FIRST_SYNC_TIMEOUT_MS) {
      handleNtpFailure(now);
    }
    return;
  }

  if (ntpRearmPending_ || ntpManualSyncPending_ || ntpActivitySyncPending_) {
    ntpRearmPending_ = false;
    ntpManualSyncPending_ = false;
    ntpActivitySyncPending_ = false;
    ntpFailoverIndex_ = 0;
    if (!armNtp(now, staConnected, gateGeneration)) {
      ntpRearmPending_ = true;
    }
    return;
  }

  switch (timeStatus.state) {
    case TimeSyncState::OFF:
    case TimeSyncState::FAILED:
      if (timeStatus.nextRetryInMs > 0) {
        return;
      }
      if (!armNtp(now, staConnected, gateGeneration)) {
        ntpRearmPending_ = true;
      }
      return;
    case TimeSyncState::SYNCED:
    case TimeSyncState::STALE:
      if (timeStatus.lastSyncAgeMs >= NTP_RESYNC_INTERVAL_MS) {
        if (!armNtp(now, staConnected, gateGeneration)) {
          ntpRearmPending_ = true;
        }
      }
      return;
    case TimeSyncState::SYNCING:
      return;
  }
}

bool ShotStopperNetwork::startHttpServer() {
  if (server_ != nullptr) {
    return true;
  }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.task_priority = tskIDLE_PRIORITY + 1;
  config.stack_size = HTTP_SERVER_TASK_STACK_SIZE;
  // Keep the httpd task stack in internal RAM (default task_caps). OTA upload
  // erases/writes flash on this task; a PSRAM stack would fault while the
  // cache is disabled.
  // Web UI serializes API traffic (DEVICE_MAX_INFLIGHT, default 1). Reserve
  // headroom for the HTML/JS/CSS boot burst plus LRU/TCP close. ESP-IDF uses
  // (max_open_sockets + 3) LWIP sockets total.
  // One active WebUI client plus a brief parallel asset fetch; 6 was oversized
  // for steady-state DRAM (each open socket carries TCP wnd/snd buffers).
  config.max_open_sockets = 4;
  // Keep several slots of headroom above the owned API routes; /api/v1/ui/unlock
  // and the four OTA routes are registered separately because they do not use
  // the exclusive WebUI claim.
  // Kept ahead of the number of routes actually registered. Exhausting this
  // makes the last registerHandler fail, which tears down the whole Web UI, so
  // check_web_assets.js fails the build before the margin is gone.
  // Shell + app.js/css/runtime + secondary + settings + 4 HTML partials + APIs.
  config.max_uri_handlers = 76;
  // Safari sends a long UA + Accept-Language + optional Cookie/Sec-Fetch-*;
  // the IDF default (1024) is enough most of the time but intermittent
  // long browser headers have returned 431 Request Header Fields Too Large.
  config.max_req_hdr_len = 2048;
  config.max_resp_headers = 12;
  config.backlog_conn = 3;
  config.lru_purge_enable = true;
  // Keep stuck clients from occupying sockets under BLE/Wi-Fi coex stalls.
  config.recv_wait_timeout = 5;
  config.send_wait_timeout = 5;
  if (httpd_start(&server_, &config) != ESP_OK) {
    server_ = nullptr;
    lifecycleLog("httpd_start failed");
    return false;
  }

  const bool registered =
      registerHandler(server_, "/", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/diagnostic", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/log", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/stats", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/admin", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/settings", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/app.js", HTTP_GET, jsHandler) &&
      registerHandler(server_, "/app.css", HTTP_GET, cssHandler) &&
      registerHandler(server_, "/js/runtime.js", HTTP_GET, runtimeJsHandler) &&
      registerHandler(server_, "/js/ota-image.js", HTTP_GET, otaImageJsHandler) &&
      registerHandler(server_, "/js/secondary.js", HTTP_GET,
                      secondaryJsHandler) &&
      registerHandler(server_, "/partials/stats.html", HTTP_GET,
                      partialStatsHandler) &&
      registerHandler(server_, "/partials/diagnostic.html", HTTP_GET,
                      partialDiagnosticHandler) &&
      registerHandler(server_, "/partials/settings.html", HTTP_GET,
                      partialSettingsHandler) &&
      registerHandler(server_, "/partials/admin.html", HTTP_GET,
                      partialAdminHandler) &&
      registerHandler(server_, "/js/settings.js", HTTP_GET,
                      viewSettingsHandler) &&
      registerHandler(server_, "/favicon.ico", HTTP_GET,
                      browserIconHandler) &&
      registerHandler(server_, "/apple-touch-icon.png", HTTP_GET,
                      browserIconHandler) &&
      registerHandler(server_, "/apple-touch-icon-precomposed.png", HTTP_GET,
                      browserIconHandler) &&
      registerHandler(server_, "/api/v1/ui/claim", HTTP_POST, claimHandler) &&
      registerHandler(server_, "/api/v1/ui/unlock", HTTP_POST, unlockHandler) &&
      registerHandler(server_, "/api/v1/integration", HTTP_GET, integrationHandler) &&
      registerHandler(server_, "/api/v1/integration/request", HTTP_GET, integrationHandler) &&
      registerHandler(server_, "/api/v1/integration/webhook", HTTP_GET, integrationHandler) &&
      registerHandler(server_, "/api/v1/integration/webhook", HTTP_PUT, integrationHandler) &&
      registerHandler(server_, "/api/v1/integration/webhook/test", HTTP_POST, integrationHandler) &&
      registerHandler(server_, "/api/v1/integration/presets", HTTP_GET, integrationHandler) &&
      registerHandler(server_, "/api/v1/integration/presets/active", HTTP_PUT, integrationHandler) &&
      registerHandler(server_, "/api/v1/integration/quick-settings", HTTP_PUT, integrationHandler) &&
      registerHandler(server_, "/api/v1/integration/restart", HTTP_POST, integrationHandler) &&
      registerHandler(server_, "/api/v1/status/home", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/status/settings", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/status/admin", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/status/diagnostic", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/debug/export", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/log", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/shots", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/shots/clear", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/shots/delete", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/shots/rate", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/last-shot/clear", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/time/sync", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/config", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/webhooks", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/bullseye/test", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/scale/preferred/clear", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/scale/preferred/select", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/presets", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/calibration/reset", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/calibration/reset-guard-samples", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/control/paddle", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/control/rinse", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/control/stop", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/control/force-pulse", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/control/state-override", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/control/restart", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/diagnostic/reset-history", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/factory-reset", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/network", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/network/scan", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/network/scan", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/device/password", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/admin/unlock", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/admin/lock", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/admin/ble-compat", HTTP_PUT, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/diagnostic/profiler", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/ota", HTTP_GET, otaStatusHandler) &&
      registerHandler(server_, "/api/v1/ota", HTTP_POST, otaUploadHandler) &&
      registerHandler(server_, "/api/v1/ota/session", HTTP_GET, otaSessionHandler) &&
      registerHandler(server_, "/api/v1/ota/session", HTTP_POST, otaSessionHandler) &&
      registerHandler(server_, "/api/v1/ota", HTTP_PATCH, otaPatchHandler) &&
      registerHandler(server_, "/api/v1/ota/flash", HTTP_POST, otaFlashHandler) &&
      registerHandler(server_, "/api/v1/ota/abort", HTTP_POST, otaAbortHandler);
  if (!registered ||
      httpd_register_err_handler(server_, HTTPD_404_NOT_FOUND,
                                 notFoundHandler) != ESP_OK) {
    stopHttpServer();
    return false;
  }
  return true;
}

void ShotStopperNetwork::stopHttpServer() {
  if (server_ != nullptr) {
    httpd_stop(server_);
    server_ = nullptr;
  }
}


esp_err_t ShotStopperNetwork::sendJson(httpd_req_t *request,
                                       const char *status,
                                       const char *json) {
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  // Close after each API response so keep-alive does not pin sockets on the
  // ESP while BLE/Wi-Fi coex delays responses.
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  const size_t length = json == nullptr ? 0 : strlen(json);
  return sendCopiedBody(request, json, length);
}

esp_err_t ShotStopperNetwork::sendError(httpd_req_t *request,
                                        const char *status,
                                        const char *error,
                                        const char *message) {
  char body[256] = {};
  snprintf(body, sizeof(body), "{\"error\":\"%s\",\"message\":\"%s\"}",
           error, message);
  return sendJson(request, status, body);
}

namespace {

constexpr const char *WEB_UI_CLIENT_HEADER = "X-WebUI-Client";
