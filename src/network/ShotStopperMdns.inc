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
  if (mdnsStarted_ || (mdnsRetryAtMs_ != 0 &&
      static_cast<int32_t>(now - mdnsRetryAtMs_) < 0)) return;
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
  if (!allowed()) stopMdns();
}

void ShotStopperNetwork::stopMdns() {
  if (!mdnsStarted_) return;
  mdns_free();
  mdnsStarted_ = false;
}
