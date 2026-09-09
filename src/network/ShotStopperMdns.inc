constexpr uint32_t MDNS_WAKE_INTERVAL_MS = 30000;
constexpr uint32_t MDNS_WAKE_SETTLE_MS = 500;
constexpr uint32_t MDNS_WAKE_SEND_MS = 500;

// Network-manager-only lifecycle. SDK calls may block; never call from control.
void ShotStopperNetwork::serviceMdns(uint32_t now, bool staConnected) {
  const uint32_t generation = rfGateGeneration_.load(std::memory_order_acquire);
  const auto allowed = [&]() {
    return staConnected && server_ != nullptr && !restartPending_ &&
           !brewRfActive() && !scaleConnecting_.load(std::memory_order_acquire) &&
           rfGateGeneration_.load(std::memory_order_acquire) == generation;
  };
  if (!allowed()) {
    stopMdns();
    return;
  }
  if (mdnsStarted_) {
    serviceMdnsWake(now, generation);
    return;
  }
  if (mdnsRetryAtMs_ != 0 &&
      static_cast<int32_t>(now - mdnsRetryAtMs_) < 0) return;
  // Bound allocation/log churn on failure, including across millis() wrap.
  mdnsRetryAtMs_ = now + 5000U;
  if (mdnsRetryAtMs_ == 0) mdnsRetryAtMs_ = 1;
  if (mdns_init() != ESP_OK) {
    lifecycleLog("mDNS init failed; retry in 5 s");
    return;
  }
  mdnsStarted_ = true;
  char name[DEVICE_NAME_CAPACITY];
  {
    TaskLockGuard lock(dataMux_);
    copyCString(name, sizeof(name), status_.deviceName);
  }
  if (!allowed() || mdns_hostname_set(name) != ESP_OK ||
      !allowed() || mdns_instance_name_set(name) != ESP_OK ||
      !allowed() || mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0) != ESP_OK) {
    stopMdns();
    return;
  }
  // A transition may arrive while the SDK is allocating/queueing records.
  if (!allowed()) {
    stopMdns();
    return;
  }
  mdnsWakeAtMs_ = now + MDNS_WAKE_INTERVAL_MS;
  if (mdnsWakeAtMs_ == 0) mdnsWakeAtMs_ = 1;
}

void ShotStopperNetwork::serviceMdnsWake(uint32_t now,
                                         uint32_t expectedGateGeneration) {
  if (rfGateGeneration_.load(std::memory_order_acquire) !=
      expectedGateGeneration) {
    cancelMdnsWake();
    return;
  }
  if (mdnsWakePhase_ == MdnsWakePhase::IDLE) {
    if (mdnsWakeAtMs_ == 0 ||
        static_cast<int32_t>(now - mdnsWakeAtMs_) < 0) return;
    mdnsWakeAtMs_ = now + MDNS_WAKE_INTERVAL_MS;
    if (mdnsWakeAtMs_ == 0) mdnsWakeAtMs_ = 1;
    wifi_ps_type_t current = WIFI_PS_NONE;
    if (esp_wifi_get_ps(&current) != ESP_OK) {
      lifecycleLog("mDNS wake skipped; WiFi power state unavailable");
      return;
    }
    if (current != WIFI_PS_MIN_MODEM) {
      if (mdns_netif_action(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"),
                            MDNS_EVENT_ANNOUNCE_IP4) != ESP_OK) {
        lifecycleLog("mDNS periodic announce failed");
      }
      return;
    }
    wifi_ps_type_t verified = WIFI_PS_MIN_MODEM;
    if (!WiFi.setSleep(WIFI_PS_NONE) ||
        esp_wifi_get_ps(&verified) != ESP_OK || verified != WIFI_PS_NONE) {
      lifecycleLog("mDNS WiFi wake failed");
      applyWifiPowerSave();
      return;
    }
    mdnsWakePhase_ = MdnsWakePhase::SETTLING;
    mdnsWakePhaseAtMs_ = now + MDNS_WAKE_SETTLE_MS;
    return;
  }
  if (static_cast<int32_t>(now - mdnsWakePhaseAtMs_) < 0) return;
  if (mdnsWakePhase_ == MdnsWakePhase::SETTLING) {
    if (mdns_netif_action(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"),
                          MDNS_EVENT_ANNOUNCE_IP4) != ESP_OK) {
      lifecycleLog("mDNS periodic announce failed");
    }
    mdnsWakePhase_ = MdnsWakePhase::SENDING;
    mdnsWakePhaseAtMs_ = now + MDNS_WAKE_SEND_MS;
    return;
  }
  mdnsWakePhase_ = MdnsWakePhase::IDLE;
  mdnsWakePhaseAtMs_ = 0;
  applyWifiPowerSave();
}

void ShotStopperNetwork::cancelMdnsWake() {
  const bool restore = mdnsWakePhase_ != MdnsWakePhase::IDLE;
  mdnsWakePhase_ = MdnsWakePhase::IDLE;
  mdnsWakeAtMs_ = 0;
  mdnsWakePhaseAtMs_ = 0;
  if (restore) applyWifiPowerSave();
}

void ShotStopperNetwork::stopMdns() {
  cancelMdnsWake();
  if (!mdnsStarted_) return;
  mdns_free();
  mdnsStarted_ = false;
}
