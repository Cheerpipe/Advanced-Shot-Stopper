namespace {

void integrationDeviceId(char output[18]) {
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    copyCString(output, 18, "unknown");
    return;
  }
  formatWifiMac(mac, output, 18);
}

bool validCorrelationId(const char *value) {
  const size_t length = value == nullptr ? 0 : strnlen(value, 65);
  if (length == 0 || length >= 65) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = value[i];
    if (!isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
      return false;
  }
  return true;
}

bool buildIntegrationPresets(const ShotPresetBank &bank, uint32_t revision,
                             char *output, size_t capacity) {
  size_t used = 0;
  if (!jsonScratchAppend(output, capacity, &used,
                         "{\"apiVersion\":1,\"activeId\":%u,"
                         "\"revision\":%lu,\"items\":[",
                         static_cast<unsigned>(bank.activeId),
                         static_cast<unsigned long>(revision))) return false;
  for (uint8_t i = 0; i < bank.count && i < MAX_SHOT_PRESETS; ++i) {
    char name[SHOT_PRESET_NAME_CAPACITY * 6] = {};
    if (!escapeJsonString(bank.presets[i].name, name, sizeof(name))) return false;
    if (!jsonScratchAppend(
            output, capacity, &used,
            "%s{\"id\":%u,\"name\":\"%s\",\"isFactory\":%s}",
            i == 0 ? "" : ",", static_cast<unsigned>(bank.presets[i].id),
            name, bank.presets[i].isFactory ? "true" : "false")) return false;
  }
  return jsonScratchAppend(output, capacity, &used, "]}");
}

bool buildIntegrationLastShot(const PersistedLastShot &shot, char *output,
                              size_t capacity) {
  const auto type = static_cast<LastShotType>(shot.shotType);
  if (!shot.valid || shot.presetId == 0 || type > LastShotType::MANUAL) {
    return snprintf(output, capacity, "null") == 4;
  }
  char name[sizeof(shot.presetName) * 6] = {};
  if (!escapeJsonString(shot.presetName, name, sizeof(name))) return false;
  size_t used = 0;
  if (!jsonScratchAppend(
          output, capacity, &used,
          "{\"cycleId\":%lu,\"uptimeMs\":%lu,\"durationMs\":%lu,"
          "\"targetWeightG\":%u,\"presetId\":%u,\"presetName\":\"%s\","
          "\"shotType\":\"%s\",\"stopDetail\":\"%s\"",
          static_cast<unsigned long>(shot.cycleId),
          static_cast<unsigned long>(shot.endedAtUptimeMs),
          static_cast<unsigned long>(shot.durationMs),
          static_cast<unsigned>(shot.goalWeightG),
          static_cast<unsigned>(shot.presetId), name, lastShotTypeName(type),
          shotLogStopDetailName(shotLogStopDetailFromEndReason(
              shot.endReason, shot.fastExtractionGuardEnabled,
              shot.extractionExtended)))) return false;
  if (shot.firstDropElapsedMs != 0 &&
      !jsonScratchAppend(output, capacity, &used, ",\"firstDropMs\":%lu",
                         static_cast<unsigned long>(shot.firstDropElapsedMs)))
    return false;
  if (shot.weightValid &&
      !jsonScratchAppend(output, capacity, &used, ",\"weightG\":%.2f",
                         shot.currentWeightG)) return false;
  if (shot.averageFlowValid &&
      !jsonScratchAppend(output, capacity, &used, ",\"averageFlowGps\":%.2f",
                         shot.averageFlowGps)) return false;
  return jsonScratchAppend(output, capacity, &used, "}");
}

bool buildIntegrationQuickSettings(const QuickSettingsSnapshot &snapshot,
                                   char *output, size_t capacity) {
  const int written = snprintf(
      output, capacity,
      "{\"revision\":%lu,\"activePresetId\":%u,\"brewByWeight\":%s,"
      "\"noScaleBbwMode\":\"%s\",\"autoToManualGuardEnabled\":%s,"
      "\"slowExtractionGuardEnabled\":%s,\"fastExtractionGuardEnabled\":%s,"
      "\"avoidAccidentalTouchEnabled\":%s,\"cupProtectionEnabled\":%s}",
      static_cast<unsigned long>(snapshot.revision),
      static_cast<unsigned>(snapshot.activePresetId),
      snapshot.brewByWeight ? "true" : "false",
      noScaleBbwModeId(snapshot.noScaleBbwMode),
      snapshot.autoToManualGuardEnabled ? "true" : "false",
      snapshot.slowExtractionGuardEnabled ? "true" : "false",
      snapshot.fastExtractionGuardEnabled ? "true" : "false",
      snapshot.avoidAccidentalTouchEnabled ? "true" : "false",
      snapshot.cupProtectionEnabled ? "true" : "false");
  return written > 0 && static_cast<size_t>(written) < capacity;
}

}  // namespace

esp_err_t ShotStopperNetwork::integrationHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  const char *uri = request->uri;

  if (apiUriMatches(uri, "/api/v1/integration") &&
      request->method == HTTP_GET) {
    if (!self.lockWorkBufForStatus()) return self.workBufBusy(request);
    ControlStatusSnapshot &control = self.workBuf_->control;
    self.loadControlStatus(control);
    RecipeSnapshot recipe;
    if (self.callbacks_.copyRecipeSnapshot != nullptr)
      self.callbacks_.copyRecipeSnapshot(&recipe);
    char lastShotJson[640] = {};
    char lastGoodShotJson[640] = {};
    char quickSettingsJson[384] = {};
    const bool lastShotOk = buildIntegrationLastShot(
        control.lastShot, lastShotJson, sizeof(lastShotJson));
    const bool lastGoodShotOk = buildIntegrationLastShot(
        control.lastGoodShot, lastGoodShotJson, sizeof(lastGoodShotJson));
    const bool quickSettingsOk = buildIntegrationQuickSettings(
        quickSettingsSnapshot(recipe), quickSettingsJson,
        sizeof(quickSettingsJson));
    const NetworkStatusSnapshot network = self.snapshot();
    char id[18] = {};
    integrationDeviceId(id);
    char firmware[32] = {};
    sanitizeJsonEmbed(FW_VERSION, firmware, sizeof(firmware));
    const int written = snprintf(
        self.workBuf_->statusJson, NetworkWorkBuf::kStatusJson,
        "{\"apiVersion\":1,\"minimumClientApiVersion\":1,"
        "\"deviceId\":\"%s\",\"manufacturer\":\"Advanced Shot Stopper\","
        "\"model\":\"Advanced Shot Stopper\",\"firmwareVersion\":\"%s\","
        "\"capabilities\":[\"webhook_v1\",\"preset_select_v1\","
        "\"quick_settings_v1\",\"restart_v1\",\"stored_shots_v1\"],"
        "\"shotState\":\"%s\",\"bootId\":%lu,\"activePresetId\":%u,"
        "\"presetRevision\":%lu,\"quickSettings\":%s,"
        "\"lastShot\":%s,\"lastGoodShot\":%s,"
        "\"lastCommand\":{\"requestId\":%lu,\"state\":\"%s\"}}",
        id, firmware, control.activeCycle ? "brewing" : "idle",
        static_cast<unsigned long>(control.bootId),
        static_cast<unsigned>(recipe.presets.activeId),
        static_cast<unsigned long>(recipe.runtime.revision), quickSettingsJson,
        lastShotJson, lastGoodShotJson,
        static_cast<unsigned long>(network.lastCommandRequestId),
        commandResultStateName(network.lastCommandState));
    const bool ok = lastShotOk && lastGoodShotOk && quickSettingsOk && written > 0 &&
                    static_cast<size_t>(written) < NetworkWorkBuf::kStatusJson;
    const esp_err_t result = ok
        ? sendJson(request, STATUS_OK, self.workBuf_->statusJson)
        : sendError(request, STATUS_SERVER_ERROR, "RESPONSE_TOO_LARGE",
                    "The integration snapshot exceeded its fixed buffer.");
    self.unlockWorkBuf();
    return result;
  }

  if (apiUriMatches(uri, "/api/v1/integration/presets") &&
      request->method == HTTP_GET) {
    if (!self.lockWorkBuf()) return self.workBufBusy(request);
    RecipeSnapshot recipe;
    if (self.callbacks_.copyRecipeSnapshot != nullptr)
      self.callbacks_.copyRecipeSnapshot(&recipe);
    const bool ok = buildIntegrationPresets(
        recipe.presets, recipe.runtime.revision, self.workBuf_->presetsJson,
        NetworkWorkBuf::kPresetsJson);
    const esp_err_t result = ok
        ? sendJson(request, STATUS_OK, self.workBuf_->presetsJson)
        : sendError(request, STATUS_SERVER_ERROR, "RESPONSE_TOO_LARGE",
                    "The preset snapshot exceeded its fixed buffer.");
    self.unlockWorkBuf();
    return result;
  }

  if (apiUriMatches(uri, "/api/v1/integration/request") &&
      request->method == HTTP_GET) {
    const NetworkStatusSnapshot network = self.snapshot();
    char response[128] = {};
    snprintf(response, sizeof(response),
             "{\"apiVersion\":1,\"requestId\":%lu,\"state\":\"%s\"}",
             static_cast<unsigned long>(network.lastCommandRequestId),
             commandResultStateName(network.lastCommandState));
    return sendJson(request, STATUS_OK, response);
  }

  if (apiUriMatches(uri, "/api/v1/integration/webhook") &&
      request->method == HTTP_GET) {
    const WebhookConfig config = self.webhooks_.config();
    char url[WEBHOOK_URL_CAPACITY * 2] = {};
    sanitizeJsonEmbed(config.url, url, sizeof(url));
    char response[640] = {};
    snprintf(response, sizeof(response),
             "{\"apiVersion\":1,\"enabled\":%s,\"url\":\"%s\","
             "\"brewState\":%s,\"firstDrop\":%s,\"end\":%s,"
             "\"presetChanges\":%s,\"deferDuringShot\":%s}",
             config.enabled ? "true" : "false", url,
             config.brewState ? "true" : "false",
             config.firstDrop ? "true" : "false",
             config.end ? "true" : "false",
             config.presetChanges ? "true" : "false",
             config.deferDuringShot ? "true" : "false");
    return sendJson(request, STATUS_OK, response);
  }

  if (apiUriMatches(uri, "/api/v1/integration/webhook/test") &&
      request->method == HTTP_POST) {
    const esp_err_t bodyStatus = self.lockJsonBody(
        request, "A bounded JSON request is required.");
    if (bodyStatus != ESP_OK) return bodyStatus;
    cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
    char correlation[65] = {};
    static const char *const fields[] = {"correlationId"};
    const bool parsed = root != nullptr &&
        jsonHasOnlyUniqueFields(root, fields, 1) &&
        jsonString(root, "correlationId", correlation, sizeof(correlation), false) &&
        validCorrelationId(correlation);
    if (root != nullptr) cJSON_Delete(root);
    self.unlockJsonBody();
    if (!parsed)
      return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                       "correlationId must be 1-64 URL-safe characters.");
    WebhookEvent event;
    event.type = WebhookEventType::TEST;
    event.uptimeMs = millis();
    ControlStatusSnapshot control = {};
    self.loadControlStatus(control);
    event.bootId = control.bootId;
    copyCString(event.correlationId, sizeof(event.correlationId), correlation);
    if (!self.webhooks_.enqueue(event))
      return sendError(request, STATUS_UNAVAILABLE, "WEBHOOK_UNAVAILABLE",
                       "The test event could not be queued.");
    return sendJson(request, STATUS_ACCEPTED,
                    "{\"apiVersion\":1,\"queued\":true}");
  }

  if (apiUriMatches(uri, "/api/v1/integration/webhook") &&
      request->method == HTTP_PUT) {
    const ControlGateSnapshot gate = self.controlGate();
    if (!controlAllowsConfiguration(gate))
      return sendError(request, STATUS_CONFLICT,
                       "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                       "Webhook settings are locked while a cycle is active.");
    const esp_err_t bodyStatus = self.lockJsonBody(
        request, "A bounded webhook JSON request is required.");
    if (bodyStatus != ESP_OK) return bodyStatus;
    cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
    WebhookConfig candidate = {};
    static const char *const fields[] = {"enabled", "url", "brewState",
        "firstDrop", "end", "presetChanges", "deferDuringShot"};
    const bool parsed = root != nullptr &&
        jsonHasOnlyUniqueFields(root, fields, 7) &&
        jsonBoolean(root, "enabled", candidate.enabled) &&
        jsonString(root, "url", candidate.url, sizeof(candidate.url), true) &&
        jsonBoolean(root, "brewState", candidate.brewState) &&
        jsonBoolean(root, "firstDrop", candidate.firstDrop) &&
        jsonBoolean(root, "end", candidate.end) &&
        jsonBoolean(root, "presetChanges", candidate.presetChanges) &&
        jsonBoolean(root, "deferDuringShot", candidate.deferDuringShot) &&
        validWebhookConfig(candidate);
    if (root != nullptr) cJSON_Delete(root);
    self.unlockJsonBody();
    if (!parsed)
      return sendError(request, STATUS_UNPROCESSABLE, "INVALID_REQUEST",
                       "A valid bounded http:// webhook configuration is required.");
    WebCommand command;
    command.type = WebCommandType::SAVE_WEBHOOK;
    command.requestId = self.allocateRequestId();
    command.webhookStageRequestId = command.requestId;
    self.dataMux_.lock();
    const bool available = self.stagedWebhookRequestId_ == 0;
    if (available) {
      self.stagedWebhook_ = candidate;
      self.stagedWebhookRequestId_ = command.requestId;
    }
    self.dataMux_.unlock();
    if (!available)
      return sendError(request, STATUS_UNAVAILABLE, "REQUEST_BUSY",
                       "Another webhook update is pending.");
    if (!self.callbacks_.enqueueWebCommand(command)) {
      self.clearStagedWebhook(command.requestId);
      return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                       "Control is busy; webhook settings were not saved.");
    }
    return self.sendAccepted(request, command.requestId,
                             "\"apiVersion\":1");
  }

  if (apiUriMatches(uri, "/api/v1/integration/quick-settings") &&
      request->method == HTTP_PUT) {
    const ControlGateSnapshot gate = self.controlGate();
    if (!controlAllowsConfiguration(gate))
      return sendError(request, STATUS_CONFLICT,
                       "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                       "Quick Settings are locked while a cycle is active.");
    const esp_err_t bodyStatus = self.lockJsonBody(
        request, "A bounded Quick Settings JSON request is required.");
    if (bodyStatus != ESP_OK) return bodyStatus;
    cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
    IntegrationQuickSettingRequest parsedRequest;
    const bool parsed = parseIntegrationQuickSettingRequest(root, parsedRequest);
    if (root != nullptr) cJSON_Delete(root);
    self.unlockJsonBody();
    if (!parsed)
      return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                       "Provide baseRevision and exactly one valid Quick Settings field.");
    RecipeSnapshot recipe;
    if (self.callbacks_.copyRecipeSnapshot != nullptr)
      self.callbacks_.copyRecipeSnapshot(&recipe);
    if (parsedRequest.baseRevision != recipe.runtime.revision)
      return sendError(request, STATUS_CONFLICT, "STALE_REVISION",
                       "Quick Settings changed; refresh and retry.");
    WebCommand command;
    command.type = WebCommandType::QUICK_SETTING;
    command.requestId = self.allocateRequestId();
    command.config = recipe.runtime;
    command.presetAction = static_cast<uint8_t>(parsedRequest.field);
    command.presetId = parsedRequest.value;
    self.dataMux_.lock();
    const bool available = self.stagedPresetRequestId_ == 0;
    if (available) self.stagedPresetRequestId_ = command.requestId;
    self.dataMux_.unlock();
    if (!available)
      return sendError(request, STATUS_CONFLICT, "REQUEST_BUSY",
                       "Another settings update is pending.");
    if (!self.callbacks_.enqueueWebCommand(command)) {
      self.dataMux_.lock();
      rollbackIntegrationStagedRequest(self.stagedPresetRequestId_,
                                       command.requestId);
      self.dataMux_.unlock();
      return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                       "Control is busy; Quick Settings were not changed.");
    }
    return self.sendAccepted(request, command.requestId,
                             "\"apiVersion\":1");
  }

  if (apiUriMatches(uri, "/api/v1/integration/restart") &&
      request->method == HTTP_POST) {
    const esp_err_t bodyStatus = self.lockJsonBody(
        request, "An empty bounded JSON object is required.");
    if (bodyStatus != ESP_OK) return bodyStatus;
    cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
    const bool parsed = parseIntegrationRestartRequest(root);
    if (root != nullptr) cJSON_Delete(root);
    self.unlockJsonBody();
    if (!parsed)
      return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                       "Restart requires an empty JSON object.");
    WebCommand command;
    command.type = WebCommandType::RESTART;
    command.requestId = self.allocateRequestId();
    if (!self.callbacks_.enqueueWebCommand(command))
      return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                       "Control is busy; restart was not queued.");
    return self.sendAccepted(request, command.requestId,
                             "\"apiVersion\":1");
  }

  if (apiUriMatches(uri, "/api/v1/integration/presets/active") &&
      request->method == HTTP_PUT) {
    const ControlGateSnapshot gate = self.controlGate();
    if (!controlAllowsConfiguration(gate))
      return sendError(request, STATUS_CONFLICT,
                       "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                       "Presets are locked while a cycle is active.");
    const esp_err_t bodyStatus = self.lockJsonBody(
        request, "A bounded JSON request is required.");
    if (bodyStatus != ESP_OK) return bodyStatus;
    cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
    uint8_t id = 0;
    static const char *const fields[] = {"id"};
    const bool parsed = root != nullptr &&
        jsonHasOnlyUniqueFields(root, fields, 1) && jsonUint8(root, "id", id) &&
        id != 0;
    if (root != nullptr) cJSON_Delete(root);
    self.unlockJsonBody();
    if (!parsed)
      return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                       "id must be a non-zero preset ID.");
    RecipeSnapshot recipe;
    if (self.callbacks_.copyRecipeSnapshot != nullptr)
      self.callbacks_.copyRecipeSnapshot(&recipe);
    if (findShotPreset(recipe.presets, id) == nullptr)
      return sendError(request, STATUS_NOT_FOUND, "PRESET_NOT_FOUND",
                       "The requested preset does not exist.");
    WebCommand command;
    command.type = WebCommandType::PRESET_OP;
    command.requestId = self.allocateRequestId();
    command.presetAction = static_cast<uint8_t>(PresetAction::APPLY);
    command.presetId = id;
    command.config = recipe.runtime;
    self.dataMux_.lock();
    const bool available = self.stagedPresetRequestId_ == 0;
    if (available) self.stagedPresetRequestId_ = command.requestId;
    self.dataMux_.unlock();
    if (!available)
      return sendError(request, STATUS_CONFLICT, "REQUEST_BUSY",
                       "Another preset update is pending.");
    if (!self.callbacks_.enqueueWebCommand(command))
      {
        self.dataMux_.lock();
        if (self.stagedPresetRequestId_ == command.requestId)
          self.stagedPresetRequestId_ = 0;
        self.dataMux_.unlock();
        return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                         "Control is busy; the preset was not changed.");
      }
    return self.sendAccepted(request, command.requestId,
                             "\"apiVersion\":1");
  }

  return sendError(request, STATUS_NOT_FOUND, "NOT_FOUND",
                   "Unknown integration API route.");
}
