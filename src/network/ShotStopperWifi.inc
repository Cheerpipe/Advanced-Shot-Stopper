bool ShotStopperNetwork::controlAllowsNetworkMutation() {
  return controlAllowsConfiguration(controlGate());
}

ControlGateSnapshot ShotStopperNetwork::controlGate() const {
  ControlGateSnapshot gate;
  if (callbacks_.copyControlGate != nullptr) {
    callbacks_.copyControlGate(gate);
  }
  return gate;
}

bool ShotStopperNetwork::lockWorkBufForStatus() {
  if (callbacks_.refreshControlStatus != nullptr) {
    callbacks_.refreshControlStatus();
  }
  return lockWorkBuf();
}

void ShotStopperNetwork::loadControlStatus(ControlStatusSnapshot &control) {
  callbacks_.copyControlStatus(control);
}

bool ShotStopperNetwork::startNetwork() {
  const NetworkSettingsSnapshot settings = networkSettingsSnapshot();
  const uint32_t now = millis();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  dataMux_.lock();
  status_.wifiConfigured = settings.staConfigured;
  copyCString(status_.apIp, sizeof(status_.apIp), AP_IP);
  dataMux_.unlock();
  publishConfiguredAddressStatus();
  if (settings.staConfigured) {
    startStation(settings, now);
    startupComplete_ = true;
    return true;
  }
  if (apAutoRaiseExhausted_ || apStartHeld_) {
    lifecycleLog(apStartHeld_ ? "SoftAP raise held at boot"
                              : "SoftAP auto-raise exhausted at boot");
    startupComplete_ = false;
    return false;
  }
  startupComplete_ = ensureAccessPoint(now);
  return startupComplete_;
}

void ShotStopperNetwork::publishConfiguredAddressStatus() {
  const NetworkSettingsSnapshot settings = networkSettingsSnapshot();
  char staSsid[WIFI_SSID_CAPACITY] = {};
  char configuredIp[16] = {};
  char configuredNetmask[16] = {};
  char configuredGateway[16] = {};
  char configuredDns1[16] = {};
  char configuredDns2[16] = {};
  if (settings.staConfigured) {
    copyCString(staSsid, sizeof(staSsid), settings.staSsid);
  }
  if (settings.staIpMode == static_cast<uint8_t>(StaIpMode::STATIC)) {
    formatIpv4(settings.staIp, configuredIp);
    formatIpv4(settings.staNetmask, configuredNetmask);
    formatIpv4(settings.staGateway, configuredGateway);
    formatIpv4(settings.staDns1, configuredDns1);
    if (!ipv4IsZero(settings.staDns2)) {
      formatIpv4(settings.staDns2, configuredDns2);
    }
  }
  dataMux_.lock();
  status_.staOpen = settings.staConfigured && settings.staOpen;
  status_.staWifiSleep = settings.staWifiSleep;
  status_.staIpMode = settings.staIpMode;
  status_.staConfigState = settings.staConfigState;
  memset(status_.staSsid, 0, sizeof(status_.staSsid));
  copyCString(status_.staSsid, sizeof(status_.staSsid), staSsid);
  copyCString(status_.configuredIp, sizeof(status_.configuredIp), configuredIp);
  copyCString(status_.configuredNetmask, sizeof(status_.configuredNetmask),
              configuredNetmask);
  copyCString(status_.configuredGateway, sizeof(status_.configuredGateway),
              configuredGateway);
  copyCString(status_.configuredDns1, sizeof(status_.configuredDns1),
              configuredDns1);
  copyCString(status_.configuredDns2, sizeof(status_.configuredDns2),
              configuredDns2);
  dataMux_.unlock();
}

void ShotStopperNetwork::armPendingConfirmWindow(uint32_t now) {
  dataMux_.lock();
  staConfirmArmed_ = true;
  staConfirmDeadlineMs_ = now + STA_CONFIRM_TIMEOUT_MS;
  dataMux_.unlock();
  ::serialTraceCategory(LogLevel::INFO, DebugCategory::NETWORK,
                        "WiFi STA config pending confirmation for 180 s");
}

void ShotStopperNetwork::clearPendingConfirmWindow() {
  dataMux_.lock();
  staConfirmArmed_ = false;
  staConfirmDeadlineMs_ = 0;
  dataMux_.unlock();
}

void ShotStopperNetwork::requestPendingNetworkConfirm() {
  dataMux_.lock();
  const bool pending =
      settings_.staConfigState ==
          static_cast<uint8_t>(StaConfigState::PENDING) &&
      settings_.staConfigured && !status_.apActive;
  if (pending) {
    pendingConfirmRequest_ = true;
  }
  dataMux_.unlock();
}

bool ShotStopperNetwork::confirmPendingNetwork(const char *reason) {
  PersistedSettings next = settingsCopy();
  NetworkStatusSnapshot status = snapshot();
  if (next.staConfigState != static_cast<uint8_t>(StaConfigState::PENDING) ||
      !next.staConfigured || status.apActive ||
      status.staState != StaState::CONNECTED) {
    return false;
  }
  next.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  copyActiveStaToLkg(next);
  overlayLiveShotSettings(next);
  if (!savePersistedSettings(next)) {
    return false;
  }
  dataMux_.lock();
  settings_ = next;
  status_.staConfigState = next.staConfigState;
  pendingConfirmRequest_ = false;
  dataMux_.unlock();
  clearPendingConfirmWindow();
  publishConfiguredAddressStatus();
  log(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
      static_cast<int32_t>(next.runtime.revision));
  ::serialTraceCategoryf(LogLevel::INFO, DebugCategory::NETWORK,
                         "WiFi STA config confirmed%s%s%s",
                         reason != nullptr && reason[0] != '\0' ? " (" : "",
                         reason != nullptr && reason[0] != '\0' ? reason : "",
                         reason != nullptr && reason[0] != '\0' ? ")" : "");
  return true;
}

bool ShotStopperNetwork::revertPendingNetwork(uint32_t,
                                              const char *reason) {
  PersistedSettings next = settingsCopy();
  if (next.staConfigState != static_cast<uint8_t>(StaConfigState::PENDING)) {
    return false;
  }
  ::serialTraceCategoryf(LogLevel::WARNING, DebugCategory::NETWORK,
                         "WiFi STA pending config reverted%s%s%s",
                         reason != nullptr && reason[0] != '\0' ? " (" : "",
                         reason != nullptr && reason[0] != '\0' ? reason : "",
                         reason != nullptr && reason[0] != '\0' ? ")" : "");
  if (!restoreLkgToActive(next)) {
    clearStaNetwork(next);
  }
  overlayLiveShotSettings(next);
  if (!savePersistedSettings(next)) {
    return false;
  }
  dataMux_.lock();
  settings_ = next;
  status_.wifiConfigured = next.staConfigured;
  status_.staConfigState = next.staConfigState;
  dataMux_.unlock();
  clearPendingConfirmWindow();
  publishConfiguredAddressStatus();
  log(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
      static_cast<int32_t>(next.runtime.revision));
  stopNtp();
  staNtpEligibleAtMs_ = 0;
  g_wallClock.markDisabled();
  // Keep staEverConnected_: SoftAP auto-raise stays boot-only after any prior
  // successful STA join this process (AP_START / reboot still available).
  if (next.staConfigured) {
    // Do not startStation/ensureAccessPoint here: this frame already holds
    // settingsCopy() plus service()/serviceStaState. httpd_start on that nest
    // overflows the 10 KiB network task (pthread TLS LoadProhibited).
    startupComplete_ = false;
    return true;
  }
  if (staEverConnected_) {
    stopHttpServer();
    WiFi.softAPdisconnect(true);
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.disconnect(false, false);
    }
    dataMux_.lock();
    status_.networkActive = false;
    status_.apActive = false;
    status_.apClients = 0;
    status_.staState = StaState::NOT_CONFIGURED;
    status_.staIp[0] = '\0';
    clearStaLinkMetrics();
    dataMux_.unlock();
    lifecycleLog(
        "WiFi STA cleared after prior connect; SoftAP suppressed (AP_START or reboot)");
    return true;
  }
  startupComplete_ = false;
  return true;
}

void ShotStopperNetwork::applyStationAddressConfig(
    const NetworkSettingsSnapshot &settings) {
  if (settings.staIpMode == static_cast<uint8_t>(StaIpMode::STATIC)) {
    const IPAddress ip(settings.staIp[0], settings.staIp[1], settings.staIp[2],
                       settings.staIp[3]);
    const IPAddress gateway(settings.staGateway[0], settings.staGateway[1],
                            settings.staGateway[2], settings.staGateway[3]);
    const IPAddress netmask(settings.staNetmask[0], settings.staNetmask[1],
                            settings.staNetmask[2], settings.staNetmask[3]);
    const IPAddress dns1(settings.staDns1[0], settings.staDns1[1],
                         settings.staDns1[2], settings.staDns1[3]);
    const IPAddress dns2(settings.staDns2[0], settings.staDns2[1],
                         settings.staDns2[2], settings.staDns2[3]);
    if (!ipv4IsZero(settings.staDns2)) {
      WiFi.config(ip, gateway, netmask, dns1, dns2);
    } else {
      WiFi.config(ip, gateway, netmask, dns1);
    }
  }
  // DHCP: do not WiFi.config(INADDR_NONE,...). That path calls STA.begin()
  // with tryConnect=true and can disconnect while STA_START is in flight.
}

void ShotStopperNetwork::clearStaLinkMetrics() {
  status_.staLinkMetricsValid = false;
  status_.staRssi = 0;
  status_.staSignalQualityPct = 0;
  status_.staBssid[0] = '\0';
}

bool ShotStopperNetwork::brewRfActive() const {
  return controlCriticalRfActive_.load(std::memory_order_acquire);
}

void ShotStopperNetwork::syncScaleLinkRf(bool connectingOrUp) {
  scaleConnectingOrUp_.store(connectingOrUp, std::memory_order_relaxed);
}

void ShotStopperNetwork::syncScaleConnectingRf(bool connecting) {
  const bool changed =
      scaleConnecting_.exchange(connecting, std::memory_order_acq_rel) !=
      connecting;
  webhooks_.setScaleConnecting(connecting);
  if (connecting) {
    // The lwIP callback may race the low-priority task that stops SNTP. Close
    // its publication gate immediately without touching network APIs here.
    ntpCallbackAccepting_.store(false, std::memory_order_release);
    ntpAbortRequested_.store(true, std::memory_order_release);
  }
  if (changed) {
    rfGateGeneration_.fetch_add(1U, std::memory_order_acq_rel);
  }
  if (changed && taskHandle_ != nullptr) {
    xTaskNotifyGive(taskHandle_);
  }
}

void ShotStopperNetwork::syncControlCriticalRf(bool active) {
  const bool changed =
      controlCriticalRfActive_.exchange(active, std::memory_order_acq_rel) !=
      active;
  webhooks_.setControlCritical(active);
  if (active) {
    ntpCallbackAccepting_.store(false, std::memory_order_release);
    ntpAbortRequested_.store(true, std::memory_order_release);
  }
  if (changed) {
    rfGateGeneration_.fetch_add(1U, std::memory_order_acq_rel);
  }
  if (changed && taskHandle_ != nullptr) {
    xTaskNotifyGive(taskHandle_);
  }
}

void ShotStopperNetwork::syncScaleHuntRf(bool huntActive) {
  scaleHuntRfActive_.store(huntActive, std::memory_order_relaxed);
}

void ShotStopperNetwork::applyWifiPowerSave() {
  const wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_OFF) {
    powerNetworkBusy.store(ShotStopperOta::instance().busy(),
                           std::memory_order_release);
    lastAppliedWifiPsValid_ = false;
    return;
  }
  bool sleepAllowed = false;
  bool apActive = false;
  bool staAssociated = false;
  dataMux_.lock();
  sleepAllowed = settings_.staWifiSleep;
  apActive = status_.apActive;
  staAssociated = status_.staState == StaState::CONNECTED;
  dataMux_.unlock();
  const bool otaBusy = ShotStopperOta::instance().busy();
  powerNetworkBusy.store(apActive || otaBusy || !staAssociated,
                         std::memory_order_release);
  const WifiPowerSaveMode desired = desiredWifiPowerSave(
      sleepAllowed, apActive, staAssociated, otaBusy);
  const wifi_ps_type_t desiredPs = desired == WifiPowerSaveMode::MAX_MODEM
                                       ? WIFI_PS_MAX_MODEM
                                       : WIFI_PS_NONE;
  wifi_ps_type_t current = WIFI_PS_NONE;
  if (esp_wifi_get_ps(&current) == ESP_OK && lastAppliedWifiPsValid_ &&
      current == desiredPs && lastAppliedWifiPs_ == desiredPs) {
    return;
  }
  if (!WiFi.setSleep(desiredPs)) {
    lastAppliedWifiPsValid_ = false;
    lifecycleLog("WiFi power save set failed");
    return;
  }
  wifi_ps_type_t verified = WIFI_PS_NONE;
  if (esp_wifi_get_ps(&verified) != ESP_OK || verified != desiredPs) {
    lastAppliedWifiPsValid_ = false;
    lifecycleLog("WiFi power save verify mismatch");
    return;
  }
  lastAppliedWifiPs_ = desiredPs;
  lastAppliedWifiPsValid_ = true;
}

bool ShotStopperNetwork::beginStationConnect(const NetworkSettingsSnapshot &settings,
                                             uint32_t now) {
  if (brewRfActive()) {
    lifecycleLog("WiFi STA associate deferred; brew RF active");
    return false;
  }
  if (scaleConnecting_.load(std::memory_order_relaxed)) {
    lifecycleLog("WiFi STA associate deferred; scale connecting");
    return false;
  }
  const bool apActive = snapshot().apActive;
  if (apActive) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }
  // Drop any UI scan so association owns the radio.
  abortWifiScan(now, false);
  // Drop a stale STA association so 10 s retries do not accumulate IDF/DHCP
  // state. wifioff=false, eraseap=false: driver and SoftAP stay up (BLE coex).
  WiFi.disconnect(false, false);
  dataMux_.lock();
  status_.staState = StaState::CONNECTING;
  status_.staIp[0] = '\0';
  clearStaLinkMetrics();
  dataMux_.unlock();
  log(DebugCategory::NETWORK, DebugCode::STA_CONNECTING);
  if (apActive) {
    lifecycleLog(settings.staIpMode == static_cast<uint8_t>(StaIpMode::STATIC)
                     ? "WiFi STA retrying with static IP; SoftAP stays up"
                     : "WiFi STA retrying; SoftAP stays up");
  } else if (settings.staIpMode == static_cast<uint8_t>(StaIpMode::STATIC)) {
    lifecycleLog("WiFi STA connecting with static IP; AP disabled");
  } else {
    lifecycleLog("WiFi STA connecting; AP disabled");
  }
  // Do NOT pre-scan and lock a BSSID: an incomplete scan (BLE coexistence)
  // often sees only the weak BSS and then forces sticky association to it.
  // Let the IDF connect path scan every channel and sort by RSSI.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  if (!feedCurrentTaskWatchdog()) {
    callbacks_.reportTaskWatchdogFault();
  }

  staConnectStartedAtMs_ = millis();
  staReconnectAttemptAtMs_ = staConnectStartedAtMs_;
  lifecycleLog("WiFi STA associating with all-channel RSSI sort");
  // begin(false): enable STA and wait for STA_START without connect().
  // WiFi.begin() uses begin(true) then connect(ssid), and that extra connect()
  // can disconnect while the start handler runs (ESP_ERR_WIFI_STOP_STATE 12308).
  if (!WiFi.STA.begin(false)) {
    lifecycleLog("WiFi STA enable failed");
    return false;
  }
  applyWifiPowerSave();
  applyStationAddressConfig(settings);
  if (!WiFi.STA.connect(settings.staSsid,
                        settings.staOpen ? nullptr : settings.staPassword)) {
    dataMux_.lock();
    status_.staState = StaState::DISCONNECTED;
    dataMux_.unlock();
    lifecycleLog("WiFi STA connect request failed");
    return false;
  }
  return true;
}

void ShotStopperNetwork::startStation(const NetworkSettingsSnapshot &settings,
                                      uint32_t now) {
  stopHttpServer();
  abortWifiScan(now, false);
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.softAPdisconnect(true);
  }
  WiFi.mode(WIFI_STA);
  // Do not clear staEverConnected_: once STA has joined this boot, SoftAP must
  // not auto-raise again (serial AP_START or reboot only).
  clearPendingConfirmWindow();
  dataMux_.lock();
  status_.networkActive = false;
  status_.apActive = false;
  status_.apClients = 0;
  status_.staState = StaState::CONNECTING;
  status_.staIp[0] = '\0';
  clearStaLinkMetrics();
  status_.windowRemainingMs = 0;
  status_.confirmRemainingMs = 0;
  dataMux_.unlock();
  publishConfiguredAddressStatus();
  if (!beginStationConnect(settings, now)) {
    dataMux_.lock();
    if (status_.staState == StaState::CONNECTING) {
      status_.staState = StaState::DISCONNECTED;
    }
    dataMux_.unlock();
  }
}

void ShotStopperNetwork::stopSoftAp(bool stopHttp) {
  if (!snapshot().apActive) {
    return;
  }
  // Keep WIFI_AP_STA (or current mode) after SoftAP stop so the fresh STA
  // association is not reset by a mode switch to WIFI_STA.
  WiFi.softAPdisconnect(true);
  if (stopHttp) {
    stopHttpServer();
  }
  dataMux_.lock();
  status_.apActive = false;
  status_.apClients = 0;
  if (stopHttp) {
    status_.networkActive = false;
  }
  status_.windowRemainingMs = 0;
  dataMux_.unlock();
  clearSoftApIdleState();
  log(DebugCategory::NETWORK, DebugCode::AP_STOPPED);
  lifecycleLog(stopHttp ? "WiFi SoftAP stopped; HTTP stopped"
                        : "WiFi SoftAP stopped; HTTP kept");
  applyWifiPowerSave();
}

void ShotStopperNetwork::stopSoftApKeepStation() {
  stopSoftAp(true);
}

void ShotStopperNetwork::stopSoftApLeaveHttp() {
  stopSoftAp(false);
}

void ShotStopperNetwork::clearSoftApIdleState() {
  apIdleDeadlineMs_ = 0;
  lastApClients_ = 0;
}

void ShotStopperNetwork::armSoftApIdleDeadline(uint32_t now) {
  if (apKeepRequested_) {
    clearSoftApIdleState();
    return;
  }
  apIdleDeadlineMs_ = now + SOFTAP_IDLE_TIMEOUT_MS;
  lastApClients_ = 0;
}

void ShotStopperNetwork::serviceSoftApIdle(uint32_t now) {
  NetworkStatusSnapshot status = snapshot();
  if (!status.apActive || apKeepRequested_) {
    if (!status.apActive) {
      clearSoftApIdleState();
    } else {
      lastApClients_ = status.apClients;
      apIdleDeadlineMs_ = 0;
    }
    return;
  }

  const uint8_t clients = status.apClients;
  if (clients > 0) {
    apIdleDeadlineMs_ = 0;
    lastApClients_ = clients;
    return;
  }

  // Zero SoftAP stations: arm/re-arm on raise or when the last client leaves.
  if (lastApClients_ > 0 || apIdleDeadlineMs_ == 0) {
    armSoftApIdleDeadline(now);
  }
  lastApClients_ = 0;

  if (apIdleDeadlineMs_ == 0 ||
      static_cast<int32_t>(now - apIdleDeadlineMs_) < 0) {
    return;
  }

  apAutoRaiseExhausted_ = true;
  clearSoftApIdleState();
  lifecycleLog("WiFi SoftAP idle timeout; SoftAP down for rest of boot");
  const bool staUp = status.staState == StaState::CONNECTED &&
                     WiFi.status() == WL_CONNECTED;
  if (staUp) {
    stopSoftApLeaveHttp();
  } else {
    stopSoftAp(true);
  }
}

bool ShotStopperNetwork::wifiScanInProgress() {
  bool busy = false;
  dataMux_.lock();
  busy = scanRequested_ || g_wifiScan.state == WifiScanState::QUEUED ||
         g_wifiScan.state == WifiScanState::RUNNING;
  dataMux_.unlock();
  return busy;
}

void ShotStopperNetwork::abortWifiScan(uint32_t now, bool logTimeout) {
  bool wasRunning = false;
  bool wasQueuedOrRequested = false;
  dataMux_.lock();
  wasRunning = g_wifiScan.state == WifiScanState::RUNNING;
  wasQueuedOrRequested =
      scanRequested_ || g_wifiScan.state == WifiScanState::QUEUED || wasRunning;
  if (wasQueuedOrRequested) {
    scanRequested_ = false;
    g_wifiScan.state = logTimeout ? WifiScanState::FAILED : WifiScanState::CANCELED;
    g_wifiScan.updatedAtMs = now;
    g_wifiScan.count = 0;
  }
  dataMux_.unlock();
  if (wasRunning) {
    esp_wifi_scan_stop();
    WiFi.scanDelete();
  }
  if (!wasQueuedOrRequested) {
    return;
  }
  log(DebugCategory::NETWORK,
      logTimeout ? DebugCode::WIFI_SCAN_ERROR : DebugCode::WIFI_SCAN_CANCELED,
      logTimeout ? -1 : 0);
  if (scanMaintenanceLeaseId_ != 0) {
    WebCommand command;
    command.type = WebCommandType::START_WIFI_SCAN;
    command.requestId = scanRequestId_;
    command.maintenanceLeaseId = scanMaintenanceLeaseId_;
    (void)enqueueMaintenanceCompletion(command, false,
                                       logTimeout ? CommandResultState::FAILED
                                                  : CommandResultState::CANCELED);
    scanMaintenanceLeaseId_ = 0;
    scanRequestId_ = 0;
  }
}

bool ShotStopperNetwork::ensureAccessPoint(uint32_t now, bool force) {
  const NetworkSettingsSnapshot settings = networkSettingsSnapshot();
  NetworkStatusSnapshot status = snapshot();
  if (!force && status.apActive && status.networkActive && server_ != nullptr) {
    return true;
  }
  if (!force && (apAutoRaiseExhausted_ || apStartHeld_)) {
    lifecycleLog(apStartHeld_ ? "SoftAP raise held"
                              : "SoftAP auto-raise exhausted");
    return false;
  }

  abortWifiScan(now, false);
  const bool keepStation = settings.staConfigured;
  const bool staLinkUp = keepStation && WiFi.status() == WL_CONNECTED;
  const bool wantHttp = !httpStartHeld_;
  const bool keepHttp = wantHttp && staLinkUp && server_ != nullptr;
  if (wantHttp && !keepHttp) {
    stopHttpServer();
  }
  if (!keepStation) {
    if (WiFi.getMode() != WIFI_OFF) {
      WiFi.disconnect(false, false);
    }
    WiFi.mode(WIFI_AP);
  } else {
    // Preserve any in-flight STA association attempt while SoftAP is raised.
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_AP_STA);
  }
  const IPAddress ip(192, 168, 4, 1);
  const bool configured =
      WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  const bool apReady = configured && WiFi.softAP(AP_SSID, settings.devicePassword);
  const bool httpReady =
      keepHttp || (wantHttp && apReady && startHttpServer());
  networkStartedAtMs_ = now;
  dataMux_.lock();
  status_.networkActive = apReady && httpReady;
  status_.apActive = apReady;
  status_.apClients = 0;
  if (!settings.staConfigured) {
    status_.staState = StaState::NOT_CONFIGURED;
  } else if (status_.staState == StaState::CONNECTED && !staLinkUp) {
    status_.staState = StaState::DISCONNECTED;
  }
  if (!staLinkUp &&
      (!keepStation || status_.staState != StaState::CONNECTED)) {
    status_.staIp[0] = '\0';
    clearStaLinkMetrics();
  }
  status_.windowRemainingMs = 0;
  dataMux_.unlock();
  log(DebugCategory::NETWORK, DebugCode::AP_STARTED, apReady, httpReady);
  lifecycleLogf("%s%s at %s",
                keepStation ? "WiFi recovery SoftAP " : "WiFi SoftAP ",
                apReady && (httpReady || !wantHttp) ? "ready" : "failed",
                AP_IP);
  if (!apReady || (wantHttp && !httpReady)) {
    if (!keepHttp) {
      stopHttpServer();
    }
    WiFi.softAPdisconnect(true);
    // Keep the driver initialized: WIFI_OFF deinits and breaks BLE coexistence.
    dataMux_.lock();
    status_.apActive = false;
    status_.networkActive = false;
    dataMux_.unlock();
    clearSoftApIdleState();
    applyWifiPowerSave();
    return false;
  }
  armSoftApIdleDeadline(now);
  applyWifiPowerSave();
  return true;
}

void ShotStopperNetwork::stopNetwork() {
  esp_wifi_scan_stop();
  WiFi.scanDelete();
  stopHttpServer();
  WiFi.softAPdisconnect(true);
  if (WiFi.getMode() != WIFI_OFF && WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(false, false);
  }
  // Do not WiFi.mode(WIFI_OFF): Arduino 3.x deinits the driver there
  // (esp_wifi_deinit) and breaks BLE VHCI coexistence.
  dataMux_.lock();
  scanRequested_ = false;
  g_wifiScan = WifiScanSnapshot{};
  status_.networkActive = false;
  status_.apActive = false;
  status_.apClients = 0;
  status_.staState = status_.wifiConfigured ? StaState::DISCONNECTED
                                            : StaState::NOT_CONFIGURED;
  status_.staIp[0] = '\0';
  clearStaLinkMetrics();
  status_.windowRemainingMs = 0;
  dataMux_.unlock();
  clearSoftApIdleState();
  log(DebugCategory::NETWORK, DebugCode::AP_STOPPED);
}

void ShotStopperNetwork::serviceStaState(uint32_t now) {
  NetworkStatusSnapshot status = snapshot();
  if (!status.wifiConfigured) {
    return;
  }

  const bool brewRf = brewRfActive();
  const bool scaleConnecting =
      scaleConnecting_.load(std::memory_order_relaxed);
  // Abort an in-flight scan/associate only. A STA that already has an IP
  // must promote to CONNECTED: associated traffic can coexist under PREFER_BT.
  if ((brewRf || scaleConnecting) && status.staState == StaState::CONNECTING &&
      WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(false, false);
    dataMux_.lock();
    status_.staState = StaState::DISCONNECTED;
    dataMux_.unlock();
    lifecycleLog(brewRf ? "WiFi STA associate aborted; brew RF active"
                        : "WiFi STA associate aborted; scale connecting");
    applyWifiPowerSave();
    return;
  }

  if (status.staState == StaState::CONNECTED) {
    if (WiFi.status() != WL_CONNECTED) {
      dataMux_.lock();
      status_.staState = StaState::DISCONNECTED;
      status_.networkActive = false;
      status_.staIp[0] = '\0';
      clearStaLinkMetrics();
      dataMux_.unlock();
      log(DebugCategory::NETWORK, DebugCode::STA_FAILED,
          static_cast<int32_t>(WiFi.status()));
      stopNtp();
      staNtpEligibleAtMs_ = 0;
      g_wallClock.markDisabled();
      // After a prior successful STA join this session, never auto-raise SoftAP
      // on link loss — only retry station (SoftAP via AP_START or reboot).
      staConnectStartedAtMs_ = now;
      staReconnectAttemptAtMs_ = now;
      lifecycleLog("WiFi STA disconnected; retrying STA (no SoftAP after prior connect)");
      applyWifiPowerSave();
      if (!staReconnectHeld_ && !wifiScanInProgress() && !brewRf &&
          !scaleConnecting) {
        beginStationConnect(networkSettingsSnapshot(), now);
      } else if (staReconnectHeld_) {
        lifecycleLog("STA reconnect held; skipping retry");
      } else if (brewRf) {
        lifecycleLog("STA reconnect deferred; brew RF active");
      } else if (scaleConnecting) {
        lifecycleLog("STA reconnect deferred; scale connecting");
      }
      return;
    }
    if (status.apActive && !apKeepRequested_) {
      stopSoftApKeepStation();
    }
    {
      const int32_t rssi = WiFi.RSSI();
      dataMux_.lock();
      status_.staLinkMetricsValid = true;
      status_.staRssi = clampWifiRssi(rssi);
      status_.staSignalQualityPct = wifiRssiToSignalQualityPct(rssi);
      dataMux_.unlock();
    }
    if (!httpStartHeld_ && server_ == nullptr &&
        static_cast<int32_t>(now - httpRetryAtMs_) >= 0) {
      const bool httpReady = startHttpServer();
      httpRetryAtMs_ = httpReady ? 0 : now + HTTP_RETRY_MS;
      dataMux_.lock();
      status_.networkActive = httpReady;
      dataMux_.unlock();
      if (!httpReady) {
        lifecycleLog("HTTP start retry failed on STA");
      }
    }
    if (staConfirmArmed_ &&
        staJoinHints().staConfigState ==
            static_cast<uint8_t>(StaConfigState::PENDING) &&
        static_cast<int32_t>(now - staConfirmDeadlineMs_) >= 0) {
      WiFi.disconnect(false, false);
      if (!revertPendingNetwork(now, "confirm timeout")) {
        startupComplete_ = false;
        if (startupFailures_ < UINT8_MAX) {
          ++startupFailures_;
        }
        networkRetryAtMs_ = now + NETWORK_RETRY_MIN_MS;
        dataMux_.lock();
        status_.startupFailures = startupFailures_;
        dataMux_.unlock();
      } else {
        startupFailures_ = 0;
        dataMux_.lock();
        status_.startupFailures = 0;
        dataMux_.unlock();
      }
    }
    return;
  }

  if ((status.staState == StaState::CONNECTING ||
       status.staState == StaState::DISCONNECTED ||
       status.staState == StaState::FAILED) &&
      WiFi.status() == WL_CONNECTED) {
    char address[16] = {};
    formatIp(WiFi.localIP(), address);
    if (!apKeepRequested_) {
      stopSoftApKeepStation();
    }
    const bool mayStartHttp = !httpStartHeld_ &&
                              static_cast<int32_t>(now - httpRetryAtMs_) >= 0;
    const bool httpReady = mayStartHttp && startHttpServer();
    httpRetryAtMs_ = httpReady || httpStartHeld_ ? 0 : now + HTTP_RETRY_MS;
    staEverConnected_ = true;
    {
      const int32_t rssi = WiFi.RSSI();
      char bssidText[18] = {};
      const uint8_t *bssid = WiFi.BSSID();
      if (bssid != nullptr) {
        formatWifiMac(bssid, bssidText, sizeof(bssidText));
      }
      dataMux_.lock();
      status_.staState = StaState::CONNECTED;
      status_.networkActive = httpReady;
      copyCString(status_.staIp, sizeof(status_.staIp), address);
      status_.staLinkMetricsValid = true;
      status_.staRssi = clampWifiRssi(rssi);
      status_.staSignalQualityPct = wifiRssiToSignalQualityPct(rssi);
      status_.windowRemainingMs = 0;
      copyCString(status_.staBssid, sizeof(status_.staBssid), bssidText);
      dataMux_.unlock();
      log(DebugCategory::NETWORK, DebugCode::STA_CONNECTED);
      lifecycleLogf("WiFi STA connected; IP: %s bssid=%s rssi=%ld http=%s",
                    address, bssidText[0] != '\0' ? bssidText : "-",
                    static_cast<long>(rssi),
                    httpReady ? "up" : (httpStartHeld_ ? "held" : "down"));
    }
    staNtpEligibleAtMs_ = now + NTP_STA_SETTLE_MS;
    ntpRearmPending_ = true;
    if (staJoinHints().staConfigState ==
        static_cast<uint8_t>(StaConfigState::PENDING)) {
      armPendingConfirmWindow(now);
    }
    applyWifiPowerSave();
    return;
  }

  if (status.staState == StaState::CONNECTING &&
      static_cast<uint32_t>(millis() - staConnectStartedAtMs_) >=
          STA_CONNECT_TIMEOUT_MS &&
      !status.apActive) {
    // SoftAP auto-raise is boot/bootstrap only (never after a prior CONNECTED).
    if (wifiScanInProgress()) {
      return;
    }
    WiFi.disconnect(false, false);
    dataMux_.lock();
    status_.staState = StaState::FAILED;
    dataMux_.unlock();
    log(DebugCategory::NETWORK, DebugCode::STA_FAILED,
        static_cast<int32_t>(WiFi.status()));
    const bool pending = staJoinHints().staConfigState ==
                         static_cast<uint8_t>(StaConfigState::PENDING);
    if (pending) {
      lifecycleLog("WiFi STA failed; reverting pending config");
      if (!revertPendingNetwork(now, "connect timeout")) {
        startupComplete_ = false;
        if (startupFailures_ < UINT8_MAX) {
          ++startupFailures_;
        }
        networkRetryAtMs_ = now + NETWORK_RETRY_MIN_MS;
        dataMux_.lock();
        status_.startupFailures = startupFailures_;
        dataMux_.unlock();
        log(DebugCategory::NETWORK, DebugCode::NETWORK_RETRY,
            startupFailures_, NETWORK_RETRY_MIN_MS);
      } else {
        startupFailures_ = 0;
        dataMux_.lock();
        status_.startupFailures = 0;
        dataMux_.unlock();
      }
      return;
    }
    if (staEverConnected_) {
      lifecycleLog("WiFi STA failed; SoftAP suppressed after prior connect");
      startupFailures_ = 0;
      dataMux_.lock();
      status_.startupFailures = 0;
      status_.staState = StaState::DISCONNECTED;
      dataMux_.unlock();
      staReconnectAttemptAtMs_ = now;
      return;
    }
    lifecycleLog("WiFi STA failed; SoftAP up, STA retry continues");
    if (apStartHeld_ || apAutoRaiseExhausted_) {
      lifecycleLog(apStartHeld_ ? "SoftAP raise held after STA fail"
                                : "SoftAP auto-raise exhausted after STA fail");
      startupFailures_ = 0;
      dataMux_.lock();
      status_.startupFailures = 0;
      status_.staState = StaState::DISCONNECTED;
      dataMux_.unlock();
    } else if (!ensureAccessPoint(now)) {
      startupComplete_ = false;
      if (startupFailures_ < UINT8_MAX) {
        ++startupFailures_;
      }
      networkRetryAtMs_ = now + NETWORK_RETRY_MIN_MS;
      dataMux_.lock();
      status_.startupFailures = startupFailures_;
      dataMux_.unlock();
      log(DebugCategory::NETWORK, DebugCode::NETWORK_RETRY,
          startupFailures_, NETWORK_RETRY_MIN_MS);
    } else {
      startupFailures_ = 0;
      dataMux_.lock();
      status_.startupFailures = 0;
      status_.staState = StaState::DISCONNECTED;
      dataMux_.unlock();
    }
    staReconnectAttemptAtMs_ = now;
    return;
  }

  // SoftAP after STA bootstrap window while still DISCONNECTED/FAILED (e.g.
  // scan delayed the reconnect begin). Never after a prior CONNECTED session,
  // AP_STOP hold, or SoftAP idle exhaustion. Also skip while brew RF is active
  // so SoftAP raise cannot steal airtime.
  if (!staEverConnected_ && !status.apActive && !brewRf && !scaleConnecting &&
      !apStartHeld_ && !apAutoRaiseExhausted_ &&
      (status.staState == StaState::DISCONNECTED ||
       status.staState == StaState::FAILED) &&
      static_cast<uint32_t>(millis() - staConnectStartedAtMs_) >=
          STA_CONNECT_TIMEOUT_MS &&
      !wifiScanInProgress()) {
    if (!ensureAccessPoint(now)) {
      startupComplete_ = false;
      if (startupFailures_ < UINT8_MAX) {
        ++startupFailures_;
      }
      networkRetryAtMs_ = now + NETWORK_RETRY_MIN_MS;
      dataMux_.lock();
      status_.startupFailures = startupFailures_;
      dataMux_.unlock();
      log(DebugCategory::NETWORK, DebugCode::NETWORK_RETRY,
          startupFailures_, NETWORK_RETRY_MIN_MS);
      return;
    }
    staReconnectAttemptAtMs_ = now;
  }

  if (status.staState == StaState::CONNECTING && status.apActive &&
      !wifiScanInProgress()) {
    const wl_status_t wifiStatus = WiFi.status();
    const bool terminalFail = wifiStatus == WL_CONNECT_FAILED ||
                              wifiStatus == WL_NO_SSID_AVAIL ||
                              wifiStatus == WL_CONNECTION_LOST;
    const bool attemptAged =
        static_cast<uint32_t>(millis() - staConnectStartedAtMs_) >=
        STA_RECOVERY_ATTEMPT_MS;
    if (terminalFail || attemptAged) {
      dataMux_.lock();
      status_.staState = StaState::DISCONNECTED;
      dataMux_.unlock();
      staReconnectAttemptAtMs_ = now;
      if (terminalFail) {
        lifecycleLog("WiFi STA recovery attempt ended; will retry");
      }
      return;
    }
  }

  if ((status.staState == StaState::DISCONNECTED ||
       status.staState == StaState::FAILED) &&
      !staReconnectHeld_ && !wifiScanInProgress() && !brewRf &&
      !scaleConnecting &&
      static_cast<uint32_t>(now - staReconnectAttemptAtMs_) >=
          STA_RECONNECT_INTERVAL_MS) {
    beginStationConnect(networkSettingsSnapshot(), now);
  }
}

void ShotStopperNetwork::serviceWifiScan(uint32_t now) {
  const ControlGateSnapshot control = controlGate();
  const bool matchingMaintenanceLease =
      scanMaintenanceLeaseId_ != 0 && control.maintenanceLeaseActive &&
      control.maintenanceLeaseId == scanMaintenanceLeaseId_ &&
      !control.activeCycle && !control.relayClosed &&
      !control.physicalActivatorOn;
  const bool safe = controlAllowsConfiguration(control) ||
                    matchingMaintenanceLease;

  bool requested = false;
  WifiScanState state;
  dataMux_.lock();
  requested = scanRequested_;
  state = g_wifiScan.state;
  dataMux_.unlock();

  if (scaleHuntRfActive_.load(std::memory_order_relaxed) ||
      scaleConnecting_.load(std::memory_order_relaxed)) {
    const bool canceled = requested || state == WifiScanState::RUNNING ||
                          state == WifiScanState::QUEUED;
    if (canceled) {
      abortWifiScan(now, false);
    }
    return;
  }

  if (!safe) {
    const bool canceled = requested || state == WifiScanState::RUNNING ||
                          state == WifiScanState::QUEUED;
    if (canceled) {
      abortWifiScan(now, false);
    }
    return;
  }

  if (requested && state == WifiScanState::QUEUED) {
    const int16_t result =
        WiFi.scanNetworks(true, false, false, 120);
    dataMux_.lock();
    scanRequested_ = false;
    if (result == WIFI_SCAN_RUNNING) {
      g_wifiScan.state = WifiScanState::RUNNING;
      g_wifiScan.updatedAtMs = now;
    }
    dataMux_.unlock();
    if (result == WIFI_SCAN_RUNNING) {
      log(DebugCategory::NETWORK, DebugCode::WIFI_SCAN_STARTED);
      return;
    }
    finishWifiScan(result, now);
    return;
  }

  if (state != WifiScanState::RUNNING) {
    return;
  }
  uint32_t scanStartedAtMs = 0;
  dataMux_.lock();
  scanStartedAtMs = g_wifiScan.updatedAtMs;
  dataMux_.unlock();
  if (static_cast<uint32_t>(now - scanStartedAtMs) >= WIFI_SCAN_TIMEOUT_MS) {
    abortWifiScan(now, true);
    return;
  }
  const int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) {
    return;
  }
  finishWifiScan(result, now);
}

void ShotStopperNetwork::finishWifiScan(int16_t resultCount, uint32_t now) {
  WifiScanSnapshot &completed = g_wifiScanWorking;
  completed = WifiScanSnapshot{};
  completed.updatedAtMs = now;
  if (resultCount < 0) {
    completed.state = WifiScanState::FAILED;
    WiFi.scanDelete();
    dataMux_.lock();
    g_wifiScan = completed;
    dataMux_.unlock();
    log(DebugCategory::NETWORK, DebugCode::WIFI_SCAN_ERROR, resultCount);
    if (scanMaintenanceLeaseId_ != 0) {
      WebCommand command;
      command.type = WebCommandType::START_WIFI_SCAN;
      command.requestId = scanRequestId_;
      command.maintenanceLeaseId = scanMaintenanceLeaseId_;
      (void)enqueueMaintenanceCompletion(command, false);
      scanMaintenanceLeaseId_ = 0;
      scanRequestId_ = 0;
    }
    return;
  }

  completed.state = WifiScanState::READY;
  // Arduino's SCAN_DONE handler already copied the IDF AP list into
  // WiFiScanClass::_scanResult. A second IDF fetch returns 0 APs.
  for (int16_t index = 0;
       index < resultCount && completed.count < MAX_WIFI_SCAN_RESULTS;
       ++index) {
    const wifi_ap_record_t *apPtr = static_cast<const wifi_ap_record_t *>(
        WiFi.getScanInfoByIndex(index));
    if (apPtr == nullptr) {
      continue;
    }
    const wifi_ap_record_t &ap = *apPtr;
    const size_t length =
        strnlen(reinterpret_cast<const char *>(ap.ssid), sizeof(ap.ssid));
    if (length == 0 || length >= WIFI_SSID_CAPACITY) {
      continue;
    }
    const int32_t rssi = ap.rssi;
    size_t duplicate = completed.count;
    for (size_t item = 0; item < completed.count; ++item) {
      if (strcmp(completed.networks[item].ssid,
                 reinterpret_cast<const char *>(ap.ssid)) == 0) {
        duplicate = item;
        break;
      }
    }
    if (duplicate < completed.count) {
      if (rssi > completed.networks[duplicate].rssi) {
        completed.networks[duplicate].rssi = rssi;
        completed.networks[duplicate].channel = ap.primary;
        completed.networks[duplicate].open = ap.authmode == WIFI_AUTH_OPEN;
      }
      continue;
    }
    WifiScanNetwork &network = completed.networks[completed.count++];
    memcpy(network.ssid, ap.ssid, length);
    network.ssid[length] = '\0';
    network.rssi = rssi;
    network.channel = ap.primary;
    network.open = ap.authmode == WIFI_AUTH_OPEN;
  }

  for (size_t outer = 1; outer < completed.count; ++outer) {
    WifiScanNetwork current = completed.networks[outer];
    size_t inner = outer;
    while (inner > 0 && completed.networks[inner - 1].rssi < current.rssi) {
      completed.networks[inner] = completed.networks[inner - 1];
      --inner;
    }
    completed.networks[inner] = current;
  }
  WiFi.scanDelete();
  dataMux_.lock();
  g_wifiScan = completed;
  dataMux_.unlock();
  log(DebugCategory::NETWORK, DebugCode::WIFI_SCAN_COMPLETE,
      completed.count, resultCount);
  if (scanMaintenanceLeaseId_ != 0) {
    WebCommand command;
    command.type = WebCommandType::START_WIFI_SCAN;
    command.requestId = scanRequestId_;
    command.maintenanceLeaseId = scanMaintenanceLeaseId_;
    (void)enqueueMaintenanceCompletion(command, true);
    scanMaintenanceLeaseId_ = 0;
    scanRequestId_ = 0;
  }
}
