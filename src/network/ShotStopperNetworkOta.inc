esp_err_t ShotStopperNetwork::networkHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlGateSnapshot status = self.controlGate();
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  char action[16] = {};
  WebCommand command;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  static const char *const forgetFields[] = {"action"};
  static const char *const confirmFields[] = {"action"};
  static const char *const saveFields[] = {
      "action", "ssid",     "password", "open",  "wifiSleep", "ipMode",
      "ip",     "netmask",  "gateway",  "dns1",  "dns2"};
  const char *networkError = nullptr;
  bool parsed = root != nullptr &&
                jsonString(root, "action", action, sizeof(action), false);
  if (parsed && strcmp(action, "confirm") != 0) {
    if (!self.requireAdminUnlock(request)) {
      if (root != nullptr) {
        cJSON_Delete(root);
      }
      self.unlockJsonBody();
      command.wipePassword();
      return ESP_OK;
    }
  }
  if (parsed && strcmp(action, "confirm") == 0) {
    parsed = jsonHasOnlyUniqueFields(root, confirmFields, 1);
    if (!parsed) {
      networkError = "Confirm request must include only action=\"confirm\".";
    } else {
      const StaJoinHints sta = self.staJoinHints();
      const NetworkStatusSnapshot network = self.snapshot();
      if (sta.staConfigState !=
              static_cast<uint8_t>(StaConfigState::PENDING) ||
          !sta.staConfigured || network.apActive ||
          network.staState != StaState::CONNECTED) {
        parsed = false;
        networkError = "No pending network configuration to confirm.";
      } else {
        self.requestPendingNetworkConfirm();
        if (root != nullptr) {
          cJSON_Delete(root);
        }
        self.unlockJsonBody();
        return self.sendAccepted(request, command.requestId,
                                 "\"state\":\"QUEUED\"");
      }
    }
  } else if (parsed && strcmp(action, "forget") == 0) {
    if (!self.webUiConfigurationAllowed(request, status)) {
      if (root != nullptr) {
        cJSON_Delete(root);
      }
      self.unlockJsonBody();
      return sendError(request, STATUS_CONFLICT,
                       "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                       "Network settings are locked while a cycle is active.");
    }
    parsed = jsonHasOnlyUniqueFields(root, forgetFields, 1);
    if (parsed) {
      command.type = WebCommandType::FORGET_NETWORK;
    } else {
      networkError = "Forget request must include only action=\"forget\".";
    }
  } else if (parsed && strcmp(action, "save") == 0) {
    if (!self.webUiConfigurationAllowed(request, status)) {
      if (root != nullptr) {
        cJSON_Delete(root);
      }
      self.unlockJsonBody();
      return sendError(request, STATUS_CONFLICT,
                       "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                       "Network settings are locked while a cycle is active.");
    }
    command.setNetworkType(WebCommandType::SAVE_NETWORK);
    char ipMode[16] = {};
    char ipText[16] = {};
    char netmaskText[16] = {};
    char gatewayText[16] = {};
    char dns1Text[16] = {};
    char dns2Text[16] = {};
    const bool hasIp = cJSON_GetObjectItemCaseSensitive(root, "ip") != nullptr;
    const bool hasNetmask =
        cJSON_GetObjectItemCaseSensitive(root, "netmask") != nullptr;
    const bool hasGateway =
        cJSON_GetObjectItemCaseSensitive(root, "gateway") != nullptr;
    const bool hasDns1 =
        cJSON_GetObjectItemCaseSensitive(root, "dns1") != nullptr;
    const bool hasDns2 =
        cJSON_GetObjectItemCaseSensitive(root, "dns2") != nullptr;
    if (!jsonHasOnlyUniqueFields(root, saveFields, 11) ||
        !jsonString(root, "ssid", command.network.ssid, sizeof(command.network.ssid), false) ||
        !jsonString(root, "password", command.network.password,
                    sizeof(command.network.password), true) ||
        !jsonBoolean(root, "open", command.network.openNetwork) ||
        !jsonBoolean(root, "wifiSleep", command.network.wifiSleep) ||
        !jsonString(root, "ipMode", ipMode, sizeof(ipMode), false)) {
      parsed = false;
      networkError =
          "Save request requires action, ssid, password, open, wifiSleep, and ipMode.";
    } else if (strcmp(ipMode, "dhcp") == 0) {
      command.network.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
      if (hasIp || hasNetmask || hasGateway || hasDns1 || hasDns2) {
        parsed = false;
        networkError = "DHCP save must not include static address fields.";
      }
    } else if (strcmp(ipMode, "static") == 0) {
      command.network.staIpMode = static_cast<uint8_t>(StaIpMode::STATIC);
      if (!hasIp || !hasNetmask || !hasGateway || !hasDns1 ||
          !jsonString(root, "ip", ipText, sizeof(ipText), false) ||
          !jsonString(root, "netmask", netmaskText, sizeof(netmaskText),
                      false) ||
          !jsonString(root, "gateway", gatewayText, sizeof(gatewayText),
                      false) ||
          !jsonString(root, "dns1", dns1Text, sizeof(dns1Text), false) ||
          (hasDns2 && !jsonString(root, "dns2", dns2Text, sizeof(dns2Text),
                                  false)) ||
          !parseIpv4(ipText, command.network.staIp) ||
          !parseIpv4(netmaskText, command.network.staNetmask) ||
          !parseIpv4(gatewayText, command.network.staGateway) ||
          !parseIpv4(dns1Text, command.network.staDns1) ||
          (hasDns2 && !parseIpv4(dns2Text, command.network.staDns2))) {
        parsed = false;
        networkError =
            "Static IP save requires valid ip, netmask, gateway, and dns1.";
      }
    } else {
      parsed = false;
      networkError = "ipMode must be \"dhcp\" or \"static\".";
    }
    if (parsed) {
      command.network.wifiSleepSpecified = true;
    }
    if (parsed && !validWifiSsid(command.network.ssid)) {
      parsed = false;
      networkError = "SSID must be 1–32 characters.";
    } else if (parsed) {
      const StaJoinHints sta = self.staJoinHints();
      const bool reusePassword = shouldReuseSavedWifiCredentials(
          command.network.ssid, command.network.password, command.network.openNetwork,
          sta.staConfigured, sta.staSsid, sta.staOpen);
      if (!reusePassword &&
          !validWifiPassword(command.network.password, command.network.openNetwork)) {
        parsed = false;
        networkError = command.network.openNetwork
                           ? "Open network password must be empty."
                           : (sta.staConfigured
                                  ? "Wi-Fi password must be 8–63 characters, "
                                    "or empty to keep the saved password."
                                  : "Wi-Fi password must be 8–63 characters for a "
                                    "secured network.");
      } else if (!validStaAddressConfig(command.network.staIpMode, command.network.staIp,
                                        command.network.staNetmask, command.network.staGateway,
                                        command.network.staDns1, command.network.staDns2)) {
        parsed = false;
        networkError =
            "Static IP, netmask, gateway, and DNS must be valid and on the "
            "same subnet (not 192.168.4.x).";
      }
    }
  } else {
    parsed = false;
    networkError = "action must be \"save\", \"forget\", or \"confirm\".";
  }
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    command.wipePassword();
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_NETWORK",
                     networkError != nullptr
                         ? networkError
                         : "Invalid network SSID or password.");
  }
  if (!self.callbacks_.enqueueWebCommand(command)) {
    command.wipePassword();
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Nothing was saved.");
  }
  command.wipePassword();
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::wifiScanStartHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Wi-Fi scanning is available only while Ready.");
  }

  WebCommand command;
  command.type = WebCommandType::START_WIFI_SCAN;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; scan was not started.");
  }

  self.dataMux_.lock();
  g_wifiScan.state = WifiScanState::QUEUED;
  self.dataMux_.unlock();

  return self.sendAccepted(request, command.requestId,
                           "\"state\":\"QUEUED\"");
}

esp_err_t ShotStopperNetwork::wifiScanStatusHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  if (!self.lockWorkBuf()) {
    return self.workBufBusy(request);
  }
  WifiScanSnapshot &scan = self.workBuf_->wifiScan;
  self.dataMux_.lock();
  scan = g_wifiScan;
  self.dataMux_.unlock();

  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  char prefix[128] = {};
  snprintf(prefix, sizeof(prefix),
           "{\"state\":\"%s\",\"updatedAtMs\":%lu,\"networks\":[",
           wifiScanStateName(scan.state),
           static_cast<unsigned long>(scan.updatedAtMs));
  if (httpd_resp_send_chunk(request, prefix, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
    self.unlockWorkBuf();
    return ESP_FAIL;
  }
  for (size_t index = 0; index < scan.count; ++index) {
    if (httpd_resp_send_chunk(request, index == 0 ? "{\"ssid\":" :
                                                  ",{\"ssid\":",
                              HTTPD_RESP_USE_STRLEN) != ESP_OK ||
        sendJsonStringChunk(request, scan.networks[index].ssid) != ESP_OK) {
      self.unlockWorkBuf();
      return ESP_FAIL;
    }
    char suffix[96] = {};
    snprintf(suffix, sizeof(suffix),
             ",\"rssi\":%ld,\"channel\":%u,\"open\":%s}",
             static_cast<long>(scan.networks[index].rssi),
             static_cast<unsigned>(scan.networks[index].channel),
             scan.networks[index].open ? "true" : "false");
    if (httpd_resp_send_chunk(request, suffix, HTTPD_RESP_USE_STRLEN) !=
        ESP_OK) {
      self.unlockWorkBuf();
      return ESP_FAIL;
    }
  }
  if (httpd_resp_send_chunk(request, "]}", 2) != ESP_OK) {
    self.unlockWorkBuf();
    return ESP_FAIL;
  }
  const esp_err_t sent = httpd_resp_send_chunk(request, nullptr, 0);
  self.unlockWorkBuf();
  return sent;
}

esp_err_t ShotStopperNetwork::devicePasswordHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "The password cannot be changed while a cycle is active.");
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  WebCommand command;
  command.setNetworkType(WebCommandType::CHANGE_DEVICE_PASSWORD);
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  static const char *const fields[] = {"newPassword"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "newPassword", command.network.password,
                 sizeof(command.network.password), false);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    command.wipePassword();
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_DEVICE_PASSWORD",
                     "New device password is required.");
  }
  if (!validDevicePassword(command.network.password)) {
    command.wipePassword();
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_DEVICE_PASSWORD",
                     "New device password must be 8–63 characters.");
  }
  if (isFactoryDefaultPassword(command.network.password)) {
    command.wipePassword();
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_DEVICE_PASSWORD",
                     "New device password cannot be the factory default.");
  }
  if (!self.callbacks_.enqueueWebCommand(command)) {
    command.wipePassword();
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Nothing was saved.");
  }
  command.wipePassword();
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::bleCompatHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  static const char *const fields[] = {"enabled", "scanIntensity"};
  bool enabled = false;
  char intensityText[12] = {};
  BleScanIntensity intensity = BleScanIntensity::NORMAL;
  const bool hasEnabled = root != nullptr && jsonFieldPresent(root, "enabled");
  const bool hasScan =
      root != nullptr && jsonFieldPresent(root, "scanIntensity");
  bool parsed = root != nullptr && jsonHasOnlyUniqueFields(root, fields, 2) &&
                (hasEnabled || hasScan);
  if (parsed && hasEnabled) {
    parsed = jsonBoolean(root, "enabled", enabled);
  }
  if (parsed && hasScan) {
    parsed = jsonString(root, "scanIntensity", intensityText,
                        sizeof(intensityText), false) &&
             parseBleScanIntensityId(intensityText, intensity);
  }
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_BLE_CONFIG",
                     "enabled must be a boolean and/or scanIntensity must be "
                     "normal, aggressive, or light.");
  }
  WebCommand command;
  if (hasEnabled) {
    command.type = enabled ? WebCommandType::BLE_COMPAT_ENABLE
                           : WebCommandType::BLE_COMPAT_DISABLE;
  } else {
    command.type = WebCommandType::BLE_SCAN_INTENSITY;
  }
  if (hasScan) {
    command.bleScanIntensitySpecified = true;
    command.bleScanIntensity = static_cast<uint8_t>(intensity);
  }
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "BLE Companion state was not changed.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::taskProfilerHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  // Public while the diagnostic page is enabled; Admin unlock otherwise.
  if (!self.diagnosticPageEnabled() && !self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  bool enabled = false;
  static const char *const fields[] = {"enabled"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonBoolean(root, "enabled", enabled);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_PROFILER_CONFIG",
                     "enabled must be a boolean.");
  }
  WebCommand command;
  command.type = enabled ? WebCommandType::TASK_PROFILER_START
                         : WebCommandType::TASK_PROFILER_STOP;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Task profiler state was not changed.");
  }
  return self.sendAccepted(request, command.requestId);
}

namespace {

struct OtaTransfer {
  ShotStopperNetwork *network = nullptr;
  httpd_req_t *request = nullptr;
};

bool readOtaHeader(httpd_req_t *request, const char *name, char *output,
                   size_t capacity) {
  if (request == nullptr || output == nullptr || capacity == 0) return false;
  const size_t length = httpd_req_get_hdr_value_len(request, name);
  return length > 0 && length + 1 <= capacity &&
         httpd_req_get_hdr_value_str(request, name, output, capacity) == ESP_OK;
}

bool parseOtaUint32(const char *text, uint32_t &value) {
  if (text == nullptr || text[0] == '\0') return false;
  uint64_t parsed = 0;
  for (const char *cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return false;
    parsed = parsed * 10U + static_cast<uint64_t>(*cursor - '0');
    if (parsed > UINT32_MAX) return false;
  }
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool validOtaTransferId(const char *value) {
  if (value == nullptr) return false;
  const size_t length = strlen(value);
  if (length < 16 || length >= OTA_TRANSFER_ID_CAPACITY) return false;
  for (size_t index = 0; index < length; ++index) {
    const char c = value[index];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  }
  return true;
}

bool normalizeOtaSha256(char value[OTA_SHA256_HEX_CAPACITY]) {
  if (value == nullptr || strlen(value) != 64) return false;
  for (size_t index = 0; index < 64; ++index) {
    char &c = value[index];
    if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

bool validOtaVersion(const char *value) {
  if (value == nullptr || value[0] == '\0') return false;
  for (const char *cursor = value; *cursor != '\0'; ++cursor) {
    if (!otaVersionCharAllowed(*cursor)) return false;
  }
  return true;
}

bool parseContentRange(const char *value, uint32_t &offset, uint32_t &end,
                       uint32_t &total) {
  if (value == nullptr || strncmp(value, "bytes ", 6) != 0) return false;
  const char *cursor = value + 6;
  char first[16] = {};
  char last[16] = {};
  char size[16] = {};
  size_t firstLength = 0, lastLength = 0, sizeLength = 0;
  while (*cursor >= '0' && *cursor <= '9' && firstLength + 1 < sizeof(first))
    first[firstLength++] = *cursor++;
  if (*cursor++ != '-') return false;
  while (*cursor >= '0' && *cursor <= '9' && lastLength + 1 < sizeof(last))
    last[lastLength++] = *cursor++;
  if (*cursor++ != '/') return false;
  while (*cursor >= '0' && *cursor <= '9' && sizeLength + 1 < sizeof(size))
    size[sizeLength++] = *cursor++;
  return *cursor == '\0' && parseOtaUint32(first, offset) &&
         parseOtaUint32(last, end) && parseOtaUint32(size, total) && end >= offset;
}

bool devicePasswordsMatch(const char *candidate, const char *expected) {
  return secretsMatch(candidate, expected);
}

const char *otaResultHttpStatus(OtaResult result) {
  switch (result) {
    case OtaResult::OK: return STATUS_OK;
    case OtaResult::UNAVAILABLE:
    case OtaResult::BUSY:
    case OtaResult::PENDING_VERIFY:
    case OtaResult::SAFETY_LOST:
    case OtaResult::NO_IDENTITY:
    case OtaResult::NOTHING_STAGED: return STATUS_CONFLICT;
    case OtaResult::SESSION_REQUIRED:
    case OtaResult::SESSION_CONFLICT:
    case OtaResult::SESSION_IDENTITY_MISMATCH:
    case OtaResult::OFFSET_MISMATCH:
    case OtaResult::SESSION_EXPIRED: return STATUS_CONFLICT;
    case OtaResult::TOO_LARGE: return STATUS_TOO_LARGE;
    case OtaResult::BAD_LENGTH:
    case OtaResult::BAD_IMAGE:
    case OtaResult::NO_TAG:
    case OtaResult::ARCH_MISMATCH:
    case OtaResult::DOWNGRADE: return STATUS_UNPROCESSABLE;
    case OtaResult::INVALID_RANGE: return STATUS_BAD_REQUEST;
    case OtaResult::HASH_MISMATCH: return STATUS_UNPROCESSABLE;
    case OtaResult::RECEIVE_FAILED: return STATUS_BAD_REQUEST;
    case OtaResult::WRITE_FAILED:
    case OtaResult::VERIFY_FAILED:
    case OtaResult::COMMIT_FAILED:
    case OtaResult::NO_MEMORY:
    case OtaResult::JOURNAL_FAILED:
    case OtaResult::INTERNAL: return STATUS_SERVER_ERROR;
  }
  return STATUS_SERVER_ERROR;
}

const char *otaResultMessage(OtaResult result) {
  switch (result) {
    case OtaResult::OK: return "The firmware image was verified.";
    case OtaResult::UNAVAILABLE:
      return "This controller has no second firmware slot, so it cannot be "
             "updated over Wi-Fi.";
    case OtaResult::BUSY: return "Another firmware update is already running.";
    case OtaResult::PENDING_VERIFY:
      return "The running firmware awaits boot confirmation. Check "
             "confirmBlockReason and confirmLastError in OTA status.";
    case OtaResult::BAD_LENGTH:
      return "The upload is too small to be a firmware image.";
    case OtaResult::TOO_LARGE:
      return "The firmware image does not fit in the update slot.";
    case OtaResult::RECEIVE_FAILED:
      return "The upload was interrupted. Resume this session from nextOffset; "
             "the accepted firmware prefix was preserved.";
    case OtaResult::BAD_IMAGE:
      return "This file is not an ESP32-S3 application image.";
    case OtaResult::NO_TAG:
      return "This image is not a Shot Stopper build.";
    case OtaResult::NO_IDENTITY:
      return "The running firmware does not say which board it was built for, "
             "so no image can be checked against it. Reflash over USB with "
             "./scripts/build --arch <board> to update over Wi-Fi again.";
    case OtaResult::SESSION_REQUIRED:
      return "Create an OTA session before sending image ranges.";
    case OtaResult::SESSION_CONFLICT:
      return "Another firmware transfer owns the update slot. Abort it before choosing another image.";
    case OtaResult::SESSION_IDENTITY_MISMATCH:
      return "The transfer identity does not match the declared image.";
    case OtaResult::OFFSET_MISMATCH:
      return "This range is not the next confirmed firmware range.";
    case OtaResult::INVALID_RANGE:
      return "Range headers must agree; offsets and non-final lengths must be "
             "aligned to 4096 bytes.";
    case OtaResult::HASH_MISMATCH:
      return "The received firmware does not match its declared SHA-256.";
    case OtaResult::SESSION_EXPIRED:
      return "The inactive firmware transfer expired. Create a new session.";
    case OtaResult::ARCH_MISMATCH:
      return "This image was built for a different controller board.";
    case OtaResult::DOWNGRADE:
      return "This image is older than the running firmware.";
    case OtaResult::WRITE_FAILED:
      return "The update slot could not be written. The running firmware was "
             "not touched.";
    case OtaResult::VERIFY_FAILED:
      return "The uploaded image failed its checksum. Nothing was flashed.";
    case OtaResult::SAFETY_LOST:
      return "The paddle moved or a shot started during the upload, so the "
             "update was cancelled. The verified image was not kept.";
    case OtaResult::NOTHING_STAGED:
      return "Upload and verify a firmware image first.";
    case OtaResult::COMMIT_FAILED:
      return "The bootloader refused the staged image. Nothing was changed.";
    case OtaResult::NO_MEMORY:
      return "Not enough memory to receive a firmware image right now.";
    case OtaResult::JOURNAL_FAILED:
      return "The OTA recovery journal could not be saved. Check Diagnostic "
             "→ NVS; the running firmware was not replaced.";
    case OtaResult::INTERNAL:
      return "This firmware cannot verify updates. Reflash over USB.";
  }
  return "The firmware update failed.";
}

void appendOtaTag(char *buffer, size_t capacity, size_t &used,
                  const char *name, const OtaImageTag &tag, bool valid,
                  const char *imageSha256 = nullptr) {
  if (used >= capacity) {
    return;
  }
  int written = 0;
  if (valid && tag.valid) {
    written = snprintf(buffer + used, capacity - used,
                       ",\"%s\":{\"arch\":\"%s\",\"version\":\"%s\","
                       "\"packed\":%lu%s%s%s}",
                       name, tag.arch, tag.version,
                       static_cast<unsigned long>(tag.packed),
                       imageSha256 != nullptr ? ",\"imageSha256\":\"" : "",
                       imageSha256 != nullptr ? imageSha256 : "",
                       imageSha256 != nullptr ? "\"" : "");
  } else {
    written = snprintf(buffer + used, capacity - used, ",\"%s\":null", name);
  }
  if (written > 0 && static_cast<size_t>(written) < capacity - used) {
    used += static_cast<size_t>(written);
  }
}

}  // namespace

bool ShotStopperNetwork::authorizeOtaRequest(httpd_req_t *request) {
  char password[WIFI_PASSWORD_CAPACITY] = {};
  char expected[WIFI_PASSWORD_CAPACITY] = {};
  const char *header = DEVICE_PASSWORD_HEADER;
  size_t length = httpd_req_get_hdr_value_len(request, header);
  if (length == 0) {
    header = DEVICE_PASSWORD_HEADER_LEGACY;
    length = httpd_req_get_hdr_value_len(request, header);
  }
  bool passwordAuthorized = false;
  if (length > 0 && length + 1 <= sizeof(password) &&
      httpd_req_get_hdr_value_str(request, header, password,
                                  sizeof(password)) == ESP_OK) {
    dataMux_.lock();
    memcpy(expected, settings_.devicePassword, sizeof(expected));
    dataMux_.unlock();
    passwordAuthorized = devicePasswordsMatch(password, expected);
  }
  memset(password, 0, sizeof(password));
  memset(expected, 0, sizeof(expected));
  if (passwordAuthorized) {
    return true;
  }
  if (adminUnlockAllowed(request)) {
    touchAdminUnlock();
    return true;
  }
  return false;
}

void ShotStopperNetwork::buildOtaJson(char *buffer, size_t capacity,
                                      const ControlGateSnapshot &control, uint32_t bootId) {
  if (buffer == nullptr || capacity == 0) {
    return;
  }
  buffer[0] = '\0';
  ShotStopperOta &ota = ShotStopperOta::instance();
  const OtaStatusSnapshot ota_ = ota.snapshot();
  const bool safe = controlAllowsConfiguration(control);
  int written = snprintf(
      buffer, capacity,
      "{\"otaProtocolVersion\":%u,\"available\":%s,\"state\":\"%s\",\"slotBytes\":%lu,\"receivedBytes\":%lu,\"expectedBytes\":%lu,\"lastResult\":\"%s\","
      "\"lastReceivedBytes\":%lu,\"lastExpectedBytes\":%lu,\"pendingVerify\":%s,"
      "\"confirmed\":%s,\"safe\":%s,\"lockReason\":\"%s\",\"passwordRequired\":true,"
      "\"passwordAvailable\":true,\"restartPending\":%s,\"runningIdentityValid\":%s,\"transferId\":\"%s\","
      "\"sha256\":\"%s\",\"sessionArch\":\"%s\","
      "\"sessionVersion\":\"%s\",\"nextOffset\":%lu,\"chunkBytes\":%lu,"
      "\"sessionActive\":%s,\"sessionExpiresInMs\":%lu,\"lastChunkSha256\":\"%s\","
      "\"abortFailures\":%lu,\"journalFailures\":%lu",
      static_cast<unsigned int>(OTA_PROTOCOL_VERSION), ota_.available ? "true" : "false",
      ShotStopperOta::stateName(ota_.state),
      static_cast<unsigned long>(ota_.slotBytes), static_cast<unsigned long>(ota_.receivedBytes),
      static_cast<unsigned long>(ota_.expectedBytes), ShotStopperOta::resultName(ota_.lastResult),
      static_cast<unsigned long>(ota_.lastReceivedBytes), static_cast<unsigned long>(ota_.lastExpectedBytes),
      ota_.pendingVerify ? "true" : "false",
      ota_.confirmed ? "true" : "false", safe ? "true" : "false",
      configLockReason(control), otaRestartPending_.load(std::memory_order_acquire) ? "true" : "false",
      ota_.running.valid && otaArchIsUsable(ota_.running.arch) ? "true" : "false",
      ota_.session.transferId, ota_.session.sha256, ota_.session.arch, ota_.session.version,
      static_cast<unsigned long>(ota_.nextOffset), static_cast<unsigned long>(ota_.chunkBytes),
      ota_.sessionActive ? "true" : "false",
      static_cast<unsigned long>(ota_.sessionExpiresInMs), ota_.lastChunkSha256,
      static_cast<unsigned long>(ota_.abortFailures),
      static_cast<unsigned long>(ota_.journalFailures));
  if (written <= 0 || static_cast<size_t>(written) >= capacity) {
    snprintf(buffer, capacity, "{\"otaProtocolVersion\":%u,\"available\":false}", static_cast<unsigned int>(OTA_PROTOCOL_VERSION));
    return;
  }
  // One byte is held back so the closing brace always has somewhere to go: an
  // object left unclosed here is spliced into the admin status and would make
  // the whole Admin page unparseable, taking unrelated settings down with it.
  size_t used = static_cast<size_t>(written);
  const size_t tagCapacity = capacity - 1;
  written = snprintf(buffer + used, tagCapacity - used,
      ",\"bootId\":%lu,\"bootState\":%ld,\"confirmBlockReason\":\"%s\","
      "\"confirmAttempts\":%lu,\"confirmLastError\":%ld",
      static_cast<unsigned long>(bootId), static_cast<long>(ota_.bootState),
      ota_.confirmBlockReason, static_cast<unsigned long>(ota_.confirmAttempts),
      static_cast<long>(ota_.confirmLastError));
  if (written <= 0 || static_cast<size_t>(written) >= tagCapacity - used) {
    snprintf(buffer, capacity, "{\"otaProtocolVersion\":%u,\"available\":false}",
             static_cast<unsigned int>(OTA_PROTOCOL_VERSION));
    return;
  }
  used += static_cast<size_t>(written);
  appendOtaTag(buffer, tagCapacity, used, "running", ota_.running,
               ota_.running.valid, ota_.runningImageSha256);
  appendOtaTag(buffer, tagCapacity, used, "staged", ota_.staged,
               ota_.stagedValid, ota_.stagedImageSha256);
  if (used + 2 > capacity) {
    snprintf(buffer, capacity, "{\"otaProtocolVersion\":%u,\"available\":false}", static_cast<unsigned int>(OTA_PROTOCOL_VERSION));
    return;
  }
  buffer[used++] = '}';
  buffer[used] = '\0';
}
esp_err_t ShotStopperNetwork::sendOtaSnapshot(httpd_req_t *request,
                                              const char *httpStatus) {
  const ControlGateSnapshot control = controlGate();
  if (!lockWorkBuf()) {
    return workBufBusy(request);
  }
  loadControlStatus(workBuf_->control);
  buildOtaJson(workBuf_->otaJson, sizeof(workBuf_->otaJson), control, workBuf_->control.bootId);
  const esp_err_t sent = sendJson(request, httpStatus, workBuf_->otaJson);
  unlockWorkBuf();
  return sent;
}

int ShotStopperNetwork::otaReadChunk(void *context, uint8_t *buffer,
                                     size_t capacity) {
  OtaTransfer *transfer = static_cast<OtaTransfer *>(context);
  if (transfer == nullptr || transfer->request == nullptr) {
    return -1;
  }
  // httpd's socket timeout is deliberately short so stuck clients release
  // sockets; a few retries absorb the stalls that Wi-Fi/BLE coexistence adds
  // without letting a genuinely dead peer hold the transfer open.
  for (uint8_t attempt = 0; attempt < OTA_RECEIVE_ATTEMPTS; ++attempt) {
    const int received = httpd_req_recv(
        transfer->request, reinterpret_cast<char *>(buffer), capacity);
    if (received > 0) {
      return received;
    }
    if (received != HTTPD_SOCK_ERR_TIMEOUT) {
      return -1;
    }
  }
  return -1;
}

bool ShotStopperNetwork::otaTransferStillSafe(void *context) {
  OtaTransfer *transfer = static_cast<OtaTransfer *>(context);
  if (transfer == nullptr || transfer->network == nullptr) {
    return false;
  }
  return controlAllowsConfiguration(transfer->network->controlGate());
}

void ShotStopperNetwork::otaTransferProgress(void *context, uint32_t received,
                                             uint32_t expected) {
  OtaTransfer *transfer = static_cast<OtaTransfer *>(context);
  if (transfer == nullptr || transfer->network == nullptr) {
    return;
  }
  transfer->network->actionLogf("ota: received %lu/%lu KiB",
                                static_cast<unsigned long>(received / 1024U),
                                static_cast<unsigned long>(expected / 1024U));
  transfer->network->touchAdminUnlock();
}
esp_err_t ShotStopperNetwork::otaStatusHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authorizeOtaRequest(request)) {
    return sendError(request, STATUS_UNAUTHORIZED, "DEVICE_PASSWORD_INVALID",
                     "Send the device password, or unlock administration first.");
  }
  ShotStopperOta::instance().expireSession(millis());
  return self.sendOtaSnapshot(request, STATUS_OK);
}
esp_err_t ShotStopperNetwork::otaUploadHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authorizeOtaRequest(request)) {
    return sendError(request, STATUS_UNAUTHORIZED, "DEVICE_PASSWORD_INVALID",
                     "Send the device password, or unlock administration first.");
  }
  return sendError(request, STATUS_CONFLICT, "OTA_SESSION_REQUIRED",
                   "Create /api/v1/ota/session, then upload PATCH ranges.");
}
esp_err_t ShotStopperNetwork::otaSessionHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authorizeOtaRequest(request)) {
    return sendError(request, STATUS_UNAUTHORIZED, "DEVICE_PASSWORD_INVALID",
                     "Send the device password, or unlock administration first.");
  }
  if (request->method == HTTP_GET) return self.sendOtaSnapshot(request, STATUS_OK);
  const ControlGateSnapshot control = self.controlGate();
  if (!controlAllowsConfiguration(control)) {
    return sendError(request, STATUS_CONFLICT, "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle and wait for Ready before updating firmware.");
  }
  if (self.otaRestartPending_.load(std::memory_order_acquire)) {
    return sendError(request, STATUS_CONFLICT, "OTA_RESTART_PENDING",
                     "A firmware image is already flashed and waiting for restart.");
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON OTA session identity is required.");
  if (bodyStatus != ESP_OK) return bodyStatus;
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  OtaSessionIdentity identity;
  static const char *const fields[] = {"size", "sha256", "arch", "version", "transferId"};
  const bool parsed = root != nullptr && jsonHasOnlyUniqueFields(root, fields, 5) &&
                      jsonUint32(root, "size", identity.size) &&
                      jsonString(root, "sha256", identity.sha256, sizeof(identity.sha256), false) &&
                      jsonString(root, "arch", identity.arch, sizeof(identity.arch), false) &&
                      jsonString(root, "version", identity.version, sizeof(identity.version), false) &&
                      jsonString(root, "transferId", identity.transferId,
                                 sizeof(identity.transferId), false) &&
                      normalizeOtaSha256(identity.sha256) &&
                      otaArchIsUsable(identity.arch) && validOtaVersion(identity.version) &&
                      validOtaTransferId(identity.transferId);
  if (root != nullptr) cJSON_Delete(root);
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_OTA_SESSION",
                     "size, sha256, arch, version, and transferId must identify one image.");
  }
  ShotStopperOta &ota = ShotStopperOta::instance();
  const OtaResult result = ota.createSession(identity, millis());
  if (result != OtaResult::OK) {
    return sendError(request, otaResultHttpStatus(result),
                     ShotStopperOta::resultName(result), otaResultMessage(result));
  }
  return self.sendOtaSnapshot(request, STATUS_OK);
}

esp_err_t ShotStopperNetwork::otaPatchHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  auto rejectRange = [request](const char *status, const char *error,
                               const char *message) {
    const esp_err_t sent = sendError(request, status, error, message);
    if (request->content_len > 0) {
      httpd_sess_trigger_close(request->handle, httpd_req_to_sockfd(request));
    }
    return sent;
  };
  if (!self.authorizeOtaRequest(request)) {
    return rejectRange(STATUS_UNAUTHORIZED, "DEVICE_PASSWORD_INVALID",
                       "Send the device password, or unlock administration first.");
  }
  if (!controlAllowsConfiguration(self.controlGate())) {
    return rejectRange(STATUS_CONFLICT, "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                       "Stop the cycle and wait for Ready before updating firmware.");
  }
  char transferId[OTA_TRANSFER_ID_CAPACITY] = {};
  char offsetText[16] = {};
  char totalText[16] = {};
  char contentRange[96] = {};
  uint32_t headerOffset = 0, offset = 0, declaredTotal = 0, rangeEnd = 0, rangeTotal = 0;
  if (!readOtaHeader(request, OTA_TRANSFER_HEADER, transferId, sizeof(transferId)) ||
      !readOtaHeader(request, OTA_OFFSET_HEADER, offsetText, sizeof(offsetText)) ||
      !readOtaHeader(request, OTA_LENGTH_HEADER, totalText, sizeof(totalText)) ||
      !readOtaHeader(request, "Content-Range", contentRange, sizeof(contentRange)) ||
      !parseOtaUint32(offsetText, headerOffset) || !parseOtaUint32(totalText, declaredTotal) ||
      !parseContentRange(contentRange, offset, rangeEnd, rangeTotal) ||
      headerOffset != offset || rangeTotal != declaredTotal || request->content_len <= 0 ||
      static_cast<uint32_t>(request->content_len) != rangeEnd - offset + 1U) {
    return rejectRange(STATUS_BAD_REQUEST, "OTA_INVALID_RANGE",
                       "PATCH must contain matching offset, length, and Content-Range headers.");
  }
  ShotStopperOta &ota = ShotStopperOta::instance();
  ota.expireSession(millis());
  const OtaStatusSnapshot snapshot = ota.snapshot();
  if (strncmp(snapshot.session.transferId, transferId, sizeof(transferId)) != 0 ||
      snapshot.expectedBytes != declaredTotal) {
    return rejectRange(STATUS_CONFLICT, "OTA_SESSION_IDENTITY_MISMATCH",
                       "This range does not belong to the active OTA session.");
  }
  const uint32_t rangeLength = static_cast<uint32_t>(request->content_len);
  if (ota.isDuplicateRange(offset, rangeLength)) {
    httpd_resp_set_hdr(request, "Connection", "close");
    const esp_err_t sent = self.sendOtaSnapshot(request, STATUS_ALREADY_REPORTED);
    httpd_sess_trigger_close(request->handle, httpd_req_to_sockfd(request));
    return sent;
  }
  OtaTransfer transfer{&self, request};
  OtaStreamIo io;
  io.read = otaReadChunk;
  io.stillSafe = otaTransferStillSafe;
  io.progress = otaTransferProgress;
  io.context = &transfer;
  const OtaResult result = ota.writeRange(offset, rangeLength, io, millis());
  if (result != OtaResult::OK) {
    const OtaStatusSnapshot failed = ota.snapshot();
    self.log(DebugCategory::NETWORK, DebugCode::OTA_UPLOAD_REJECTED,
             static_cast<int32_t>(result),
             static_cast<int32_t>(failed.lastReceivedBytes / 1024U));
    return rejectRange(otaResultHttpStatus(result), ShotStopperOta::resultName(result),
                       otaResultMessage(result));
  }
  const OtaStatusSnapshot updated = ota.snapshot();
  if (updated.stagedValid) {
    self.log(DebugCategory::NETWORK, DebugCode::OTA_IMAGE_STAGED,
             static_cast<int32_t>(updated.receivedBytes / 1024U),
             static_cast<int32_t>(updated.staged.packed));
  }
  return self.sendOtaSnapshot(request, STATUS_OK);
}

esp_err_t ShotStopperNetwork::otaFlashHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authorizeOtaRequest(request)) {
    return sendError(request, STATUS_UNAUTHORIZED, "DEVICE_PASSWORD_INVALID",
                     "Send the device password, or unlock administration first.");
  }
  ShotStopperOta &ota = ShotStopperOta::instance();
  const ControlGateSnapshot control = self.controlGate();
  if (!controlAllowsConfiguration(control)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle and wait for Ready before flashing.");
  }
  if (self.otaRestartPending_.load(std::memory_order_acquire)) {
    return sendError(request, STATUS_CONFLICT, "OTA_RESTART_PENDING",
                     "The flashed image is already waiting for the restart.");
  }
  const OtaImageTag staged = ota.snapshot().staged;
  const OtaResult result = ota.commit();
  if (result != OtaResult::OK) {
    return sendError(request, otaResultHttpStatus(result),
                     ShotStopperOta::resultName(result),
                     otaResultMessage(result));
  }
  self.log(DebugCategory::NETWORK, DebugCode::OTA_FLASH_COMMITTED,
           static_cast<int32_t>(staged.packed));

  // The restart runs through the ordinary maintenance lease after the shot
  // (if any) ends. The control task will not open K1 to make way for it.
  WebCommand command;
  command.type = WebCommandType::RESTART;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    // The boot slot is already switched, so the new firmware comes up on the
    // next restart however it happens.
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "The image is flashed. Restart the controller to use it.");
  }
  self.otaRestartRequestedAtMs_.store(millis(), std::memory_order_relaxed);
  self.otaRestartPending_.store(true, std::memory_order_release);
  return self.sendOtaSnapshot(request, STATUS_ACCEPTED);
}

esp_err_t ShotStopperNetwork::otaAbortHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authorizeOtaRequest(request)) {
    return sendError(request, STATUS_UNAUTHORIZED, "DEVICE_PASSWORD_INVALID",
                     "Send the device password, or unlock administration first.");
  }
  ShotStopperOta::instance().discard();
  return self.sendOtaSnapshot(request, STATUS_OK);
}

void ShotStopperNetwork::serviceOtaRollback(uint32_t now) {
  // A committed image boots on the next restart however it happens, so a
  // restart the control task never granted must not leave the panel stuck
  // claiming one is on the way.
  if (otaRestartPending_.load(std::memory_order_acquire) &&
      static_cast<uint32_t>(
          now - otaRestartRequestedAtMs_.load(std::memory_order_relaxed)) >=
          OTA_RESTART_GIVE_UP_MS) {
    otaRestartPending_.store(false, std::memory_order_release);
    actionLog("ota: the restart never happened; restart manually to use the "
              "flashed image");
  }

  ShotStopperOta &ota = ShotStopperOta::instance();
  const OtaPublishedState otaStatus = ota.publishedState();
  // Never wait behind an HTTP upload's OTA mutex in this watchdog task.
  if (otaStatus.rejected || otaStatus.confirmed || otaStatus.busy) return;
  const ControlGateSnapshot control = controlGate();
  const OtaBootStatus boot = ota.serviceBoot(now, startupComplete_ && server_ != nullptr,
      control.activeCycle, control.relayClosed,
      scaleConnectingOrUp_.load(std::memory_order_relaxed),
      OTA_CONFIRM_MIN_UPTIME_MS, OTA_CONFIRM_DEADLINE_MS);
  if (strcmp(otaBootReason_, boot.reason) != 0 || otaBootLastError_ != boot.lastError) {
    actionLogf("ota: boot=%ld reason=%s attempts=%lu error=%ld",
        static_cast<long>(boot.state), boot.reason,
        static_cast<unsigned long>(boot.attempts), static_cast<long>(boot.lastError));
    otaBootReason_ = boot.reason;
    otaBootLastError_ = boot.lastError;
  }
  if (ota.runningImageConfirmed()) {
    log(DebugCategory::NETWORK, strcmp(boot.reason, "NO_ROLLBACK") == 0
        ? DebugCode::OTA_ROLLBACK_FAILED : DebugCode::OTA_IMAGE_CONFIRMED);
    return;
  }
  if (!ota.runningImageRejected()) return;
  log(DebugCategory::NETWORK, DebugCode::OTA_ROLLBACK_ARMED,
      static_cast<int32_t>(now / 1000U));
  actionLog("ota: no Web UI after the update; rolling back on restart");
  otaRollbackRestartPending_ = true;
  restartPending_ = true;
  restartRequestedAtMs_ = now;
}

}  // namespace shotstopper
