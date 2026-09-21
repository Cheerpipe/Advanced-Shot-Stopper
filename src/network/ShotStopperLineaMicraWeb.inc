// Maps the grace seconds accepted by the Web API to its packed option code.
bool lineaMicraGraceCodeFromSeconds(int16_t seconds, uint8_t &code) {
  for (uint8_t index = 0; index <= LINEA_MICRA_SHUTDOWN_GRACE_CODE_MAX;
       ++index) {
    if (LINEA_MICRA_SHUTDOWN_GRACE_SECONDS[index] ==
        static_cast<uint16_t>(seconds)) {
      code = index;
      return true;
    }
  }
  return false;
}

esp_err_t ShotStopperNetwork::lineaMicraHandler(httpd_req_t *request) {
#if SHOT_STOPPER_MACHINE_INTEGRATION != SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD
  return sendError(request, "404 Not Found", "UNSUPPORTED_MACHINE",
                   "Linea Micra integration is not compiled in this firmware.");
#else
  ShotStopperNetwork &self = *instance_;
  const ControlGateSnapshot gate = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, gate)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Linea Micra settings can be changed only while Ready.");
  }
  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "A bounded Linea Micra JSON request is required.");
  if (bodyStatus != ESP_OK) return bodyStatus;

  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  static const char *const fields[] = {
      "action", "username", "password", "serial", "applyTemperature",
      "observeState", "recognizeWakeGesture", "shutdownWithScale",
      "shutdownGraceSeconds"};
  char action[16] = {};
  char username[LINEA_MICRA_USERNAME_CAPACITY] = {};
  char password[LINEA_MICRA_PASSWORD_CAPACITY] = {};
  char serial[LINEA_MICRA_SERIAL_CAPACITY] = {};
  const bool known = root != nullptr &&
      jsonHasOnlyUniqueFields(root, fields, sizeof(fields) / sizeof(fields[0]));
  const bool actionOk = known &&
      jsonString(root, "action", action, sizeof(action), false);
  const char *parseError = actionOk ? nullptr
                                    : "action is required and all fields must be known.";
  bool applyTemperature = false;
  bool observeState = false;
  bool recognizeWakeGesture = false;
  bool shutdownWithScale = false;
  int16_t shutdownGraceSeconds = -1;
  uint8_t shutdownGraceCode = 0;
  LineaMicraPersistedSettings candidate = {};
  struct SensitiveWiper {
    char *username = nullptr;
    char *password = nullptr;
    LineaMicraPersistedSettings &settings;
    ~SensitiveWiper() {
      memset(username, 0, LINEA_MICRA_USERNAME_CAPACITY);
      memset(password, 0, LINEA_MICRA_PASSWORD_CAPACITY);
      wipeLineaMicraSettings(settings);
    }
  } sensitiveWiper{username, password, candidate};
  uint32_t currentRevision = 0;
  self.dataMux_.lock();
  candidate = self.settings_.lineaMicra;
  currentRevision = self.settings_.storageRevision;
  self.dataMux_.unlock();

  if (parseError == nullptr && strcmp(action, "connect") == 0) {
    if (!jsonString(root, "username", username, sizeof(username), false) ||
        !jsonString(root, "password", password, sizeof(password), false) ||
        jsonFieldPresent(root, "serial") ||
        jsonFieldPresent(root, "applyTemperature") ||
        jsonFieldPresent(root, "observeState") ||
        jsonFieldPresent(root, "recognizeWakeGesture") ||
        jsonFieldPresent(root, "shutdownWithScale") ||
        jsonFieldPresent(root, "shutdownGraceSeconds")) {
      parseError = "connect requires only username and password.";
    }
  } else if (parseError == nullptr && strcmp(action, "select") == 0) {
    if (!jsonString(root, "serial", serial, sizeof(serial), false) ||
        !jsonBoolean(root, "applyTemperature", applyTemperature) ||
        !jsonBoolean(root, "observeState", observeState) ||
        !jsonBoolean(root, "recognizeWakeGesture", recognizeWakeGesture) ||
        !jsonBoolean(root, "shutdownWithScale", shutdownWithScale) ||
        !jsonInt16(root, "shutdownGraceSeconds", shutdownGraceSeconds) ||
        !lineaMicraGraceCodeFromSeconds(shutdownGraceSeconds,
                                        shutdownGraceCode) ||
        jsonFieldPresent(root, "username") ||
        jsonFieldPresent(root, "password")) {
      parseError = "select requires serial and all machine options.";
    } else {
      wipeLineaMicraSettings(candidate);
      if (!selectMachineIntegrationDevice(serial, candidate)) {
        parseError = "Select a machine returned by the latest account connection.";
      } else {
        setLineaMicraOptions(candidate, applyTemperature, observeState,
                             recognizeWakeGesture, shutdownWithScale,
                             shutdownGraceCode);
      }
    }
  } else if (parseError == nullptr && strcmp(action, "save") == 0) {
    if (!candidate.accountConfigured ||
        !jsonBoolean(root, "applyTemperature", applyTemperature) ||
        !jsonBoolean(root, "observeState", observeState) ||
        !jsonBoolean(root, "recognizeWakeGesture", recognizeWakeGesture) ||
        !jsonBoolean(root, "shutdownWithScale", shutdownWithScale) ||
        !jsonInt16(root, "shutdownGraceSeconds", shutdownGraceSeconds) ||
        !lineaMicraGraceCodeFromSeconds(shutdownGraceSeconds,
                                        shutdownGraceCode) ||
        jsonFieldPresent(root, "username") ||
        jsonFieldPresent(root, "password") ||
        jsonFieldPresent(root, "serial")) {
      parseError = "save requires a selected machine and all machine options.";
    } else {
      setLineaMicraOptions(candidate, applyTemperature, observeState,
                           recognizeWakeGesture, shutdownWithScale,
                           shutdownGraceCode);
    }
  } else if (parseError == nullptr &&
             (strcmp(action, "disconnect") == 0 ||
              strcmp(action, "refresh") == 0)) {
    if (jsonFieldPresent(root, "username") ||
        jsonFieldPresent(root, "password") ||
        jsonFieldPresent(root, "serial") ||
        jsonFieldPresent(root, "applyTemperature") ||
        jsonFieldPresent(root, "observeState") ||
        jsonFieldPresent(root, "recognizeWakeGesture") ||
        jsonFieldPresent(root, "shutdownWithScale") ||
        jsonFieldPresent(root, "shutdownGraceSeconds")) {
      parseError = "disconnect and refresh do not accept setting fields.";
    }
  } else if (parseError == nullptr) {
    parseError = "Unknown Linea Micra action.";
  }

  cJSON *passwordField = root == nullptr
                             ? nullptr
                             : cJSON_GetObjectItemCaseSensitive(root, "password");
  if (passwordField != nullptr && cJSON_IsString(passwordField) &&
      passwordField->valuestring != nullptr) {
    memset(passwordField->valuestring, 0, strlen(passwordField->valuestring));
  }
  if (root != nullptr) cJSON_Delete(root);
  memset(self.workBuf_->requestBody, 0, sizeof(self.workBuf_->requestBody));
  self.unlockJsonBody();
  if (parseError != nullptr) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     parseError);
  }

  if (strcmp(action, "connect") == 0) {
    const NetworkStatusSnapshot network = self.snapshot();
    const bool staConnected = network.staState == StaState::CONNECTED &&
                              network.wifiConfigured &&
                              network.staIp[0] != '\0';
    if (!staConnected || network.apActive) {
      return sendError(request, STATUS_CONFLICT, "MICRA_STA_REQUIRED",
                       "Connect to Wi-Fi station mode and close setup AP first.");
    }
    const uint32_t requestId = self.allocateRequestId();
    if (!queueMachineIntegrationConnect(requestId, username, password)) {
      return sendError(request, STATUS_UNAVAILABLE, "MICRA_BUSY",
                       "The Linea Micra integration is busy; try again shortly.");
    }
    return self.sendAccepted(request, requestId);
  }

  if (strcmp(action, "refresh") == 0) {
    if (!candidate.accountConfigured) {
      return sendError(request, STATUS_CONFLICT, "MICRA_NOT_READY",
                       "Connect an account and select a machine first.");
    }
    LineaMicraRequest integrationRequest = {};
    integrationRequest.requestId = self.allocateRequestId();
    integrationRequest.configGeneration = currentRevision;
    integrationRequest.type = LineaMicraRequestType::OBSERVE_STATE;
    if (!queueMachineIntegrationRequest(integrationRequest)) {
      return sendError(request, STATUS_UNAVAILABLE, "MICRA_BUSY",
                       "The Linea Micra integration is busy; try again shortly.");
    }
    return self.sendAccepted(request, integrationRequest.requestId);
  }

  if (strcmp(action, "disconnect") == 0) {
    disconnectLineaMicra(candidate);
    clearMachineIntegrationDiscovery();
  }
  if (!validLineaMicraSettings(candidate)) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_MICRA_CONFIG",
                     "The Linea Micra settings are not valid.");
  }
  WebCommand command;
  command.type = WebCommandType::SAVE_LINEA_MICRA;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  command.stagedConfigRequestId = command.requestId;
  self.dataMux_.lock();
  const bool available = self.stagedLineaMicraRequestId_ == 0;
  if (available) {
    self.stagedLineaMicra_ = candidate;
    self.stagedLineaMicraRequestId_ = command.requestId;
  }
  self.dataMux_.unlock();
  if (!available) {
    return sendError(request, STATUS_UNAVAILABLE, "MICRA_BUSY",
                     "Another Linea Micra change is still being saved.");
  }
  if (!self.callbacks_.enqueueWebCommand(command)) {
    self.clearStagedLineaMicra(command.requestId);
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; Linea Micra settings were not saved.");
  }
  return self.sendAccepted(request, command.requestId);
#endif
}
