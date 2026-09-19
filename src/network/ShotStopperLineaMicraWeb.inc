esp_err_t ShotStopperNetwork::lineaMicraHandler(httpd_req_t *request) {
#if SHOT_STOPPER_MACHINE_INTEGRATION != SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_BLE
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
      "action", "token", "address", "clearToken", "applyTemperature",
      "observeState"};
  char action[16] = {};
  char address[LINEA_MICRA_ADDRESS_CAPACITY] = {};
  const bool known = root != nullptr &&
      jsonHasOnlyUniqueFields(root, fields, sizeof(fields) / sizeof(fields[0]));
  const bool actionOk = known &&
      jsonString(root, "action", action, sizeof(action), false);
  LineaMicraPersistedSettings candidate = {};
  struct CandidateWiper {
    LineaMicraPersistedSettings &settings;
    ~CandidateWiper() { wipeLineaMicraSettings(settings); }
  } candidateWiper{candidate};
  cJSON *tokenField = root == nullptr
                          ? nullptr
                          : cJSON_GetObjectItemCaseSensitive(root, "token");
  cJSON *addressField = root == nullptr
                            ? nullptr
                            : cJSON_GetObjectItemCaseSensitive(root, "address");
  uint32_t currentRevision = 0;
  self.dataMux_.lock();
  candidate = self.settings_.lineaMicra;
  currentRevision = self.settings_.storageRevision;
  self.dataMux_.unlock();
  const char *parseError = nullptr;

  if (!actionOk) {
    parseError = "action is required and all fields must be known.";
  } else if (strcmp(action, "save") == 0) {
    bool applyTemperature = false;
    bool observeState = false;
    bool clearToken = false;
    const bool clearPresent = jsonFieldPresent(root, "clearToken");
    if (!jsonBoolean(root, "applyTemperature", applyTemperature) ||
        !jsonBoolean(root, "observeState", observeState) ||
        (clearPresent && !jsonBoolean(root, "clearToken", clearToken))) {
      parseError = "save requires boolean applyTemperature and observeState.";
    } else if (clearToken && jsonFieldPresent(root, "token")) {
      parseError = "token and clearToken cannot be used together.";
    } else {
      if (clearToken) {
        clearLineaMicraToken(candidate);
      } else if (tokenField != nullptr &&
                 (!cJSON_IsString(tokenField) ||
                  tokenField->valuestring == nullptr ||
                  !setLineaMicraToken(candidate, tokenField->valuestring,
                                      strlen(tokenField->valuestring)))) {
        parseError = "token must contain exactly 64 printable characters.";
      }
      if (parseError == nullptr && addressField != nullptr &&
          (!jsonString(root, "address", address, sizeof(address), true) ||
           !setLineaMicraTargetAddress(candidate, address))) {
        parseError = "address must be empty or a BLE address AA:BB:CC:DD:EE:FF.";
      }
      setLineaMicraOptions(candidate, applyTemperature, observeState);
    }
  } else if (jsonFieldPresent(root, "token") ||
             jsonFieldPresent(root, "address") ||
             jsonFieldPresent(root, "clearToken") ||
             jsonFieldPresent(root, "applyTemperature") ||
             jsonFieldPresent(root, "observeState")) {
    parseError = "Only save accepts Linea Micra setting fields.";
  } else if (strcmp(action, "forget") == 0) {
    clearLineaMicraBinding(candidate);
  } else if (strcmp(action, "remove_token") == 0) {
    clearLineaMicraToken(candidate);
  } else if (strcmp(action, "test") != 0 &&
             strcmp(action, "refresh") != 0) {
    parseError = "Unknown Linea Micra action.";
  }

  if (tokenField != nullptr && tokenField->valuestring != nullptr) {
    memset(tokenField->valuestring, 0, strlen(tokenField->valuestring));
  }
  if (root != nullptr) cJSON_Delete(root);
  self.unlockJsonBody();
  if (parseError != nullptr) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     parseError);
  }

  if (strcmp(action, "test") == 0 || strcmp(action, "refresh") == 0) {
    const bool refresh = strcmp(action, "refresh") == 0;
    if (strnlen(candidate.token, sizeof(candidate.token)) != 64 ||
        (refresh && (!candidate.bindingVerified ||
                     (candidate.options & LINEA_MICRA_OBSERVE_STATE) == 0))) {
      return sendError(request, STATUS_CONFLICT, "MICRA_NOT_READY",
                       refresh
                           ? "Save a token, pair the Micra, and enable state monitoring first."
                           : "Save a valid token before testing the Micra.");
    }
    LineaMicraRequest integrationRequest = {};
    integrationRequest.requestId = self.allocateRequestId();
    integrationRequest.configGeneration = currentRevision;
    integrationRequest.type = refresh ? LineaMicraRequestType::OBSERVE_STATE
                                      : LineaMicraRequestType::TEST;
    if (!queueMachineIntegrationRequest(integrationRequest)) {
      return sendError(request, STATUS_UNAVAILABLE, "MICRA_BUSY",
                       "The Linea Micra integration is busy; try again shortly.");
    }
    return self.sendAccepted(request, integrationRequest.requestId);
  }

  if (!validLineaMicraSettings(candidate)) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_MICRA_CONFIG",
                     "The Linea Micra settings are not valid.");
  }
  WebCommand command;
  command.type = WebCommandType::SAVE_LINEA_MICRA;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  command.webhookStageRequestId = command.requestId;
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
