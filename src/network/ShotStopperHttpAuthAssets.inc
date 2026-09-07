const char *configLockReason(const ControlGateSnapshot &status) {
  if (status.activeCycle) return "active_shot";
  if (status.machineRunning) return "machine_running";
  if (status.physicalActivatorOn) return "activator_on";
  if (status.maintenanceLeaseActive) return "maintenance";
  if (status.state != StopperState::READY) return "not_ready";
  return "none";
}
const char *configLockReason(const ControlStatusSnapshot &status) {
  return configLockReason(controlGateOf(status));
}

bool apiUriMatches(const char *uri, const char *path) {
  if (uri == nullptr || path == nullptr) {
    return false;
  }
  const size_t length = strlen(path);
  return strncmp(uri, path, length) == 0 &&
         (uri[length] == '\0' || uri[length] == '?');
}

bool readWebUiClientId(httpd_req_t *request, char *output,
                       size_t capacity) {
  if (request == nullptr || output == nullptr || capacity < 2) {
    return false;
  }
  const size_t length =
      httpd_req_get_hdr_value_len(request, WEB_UI_CLIENT_HEADER);
  if (length < 16 || length + 1 > capacity) {
    return false;
  }
  if (httpd_req_get_hdr_value_str(request, WEB_UI_CLIENT_HEADER, output,
                                   capacity) != ESP_OK) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    const char character = output[index];
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      output[0] = '\0';
      return false;
    }
  }
  return true;
}

}  // namespace

esp_err_t ShotStopperNetwork::claimHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  char clientId[WEB_UI_CLIENT_ID_CAPACITY] = {};
  if (!readWebUiClientId(request, clientId, sizeof(clientId))) {
    return sendError(request, STATUS_BAD_REQUEST, "UI_CLIENT_INVALID",
                     "X-WebUI-Client must be a 16-24 character lowercase hex id.");
  }
  self.dataMux_.lock();
  memcpy(self.activeWebUiClientId_, clientId, sizeof(clientId));
  self.webUiOverrideActive_ = false;
  self.webUiOverrideUntilMs_ = 0;
  self.dataMux_.unlock();
  self.clearAdminUnlock();
  memset(clientId, 0, sizeof(clientId));
  return sendJson(request, STATUS_OK, "{\"active\":true}");
}

esp_err_t ShotStopperNetwork::unlockHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireActiveWebUiClient(request)) return ESP_OK;
  if (!self.requireAdminUnlock(request)) return ESP_OK;
  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "An explicit confirmation is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  char confirmation[32] = {};
  static const char *const fields[] = {"confirm"};
  const bool confirmed = root != nullptr &&
                         jsonHasOnlyUniqueFields(root, fields, 1) &&
                         jsonString(root, "confirm", confirmation,
                                    sizeof(confirmation), false) &&
                         strcmp(confirmation, "UNSAFE_WEBUI_OVERRIDE") == 0;
  if (root != nullptr) cJSON_Delete(root);
  self.unlockJsonBody();
  memset(confirmation, 0, sizeof(confirmation));
  if (!confirmed) {
    return sendError(request, STATUS_UNPROCESSABLE, "UNLOCK_NOT_CONFIRMED",
                     "The unsafe WebUI override was not explicitly confirmed.");
  }
  const uint32_t now = millis();
  self.dataMux_.lock();
  self.webUiOverrideActive_ = true;
  self.webUiOverrideUntilMs_ = now + WEB_UI_OVERRIDE_MS;
  self.dataMux_.unlock();
  return sendJson(request, STATUS_OK, "{\"unlocked\":true}");
}

bool ShotStopperNetwork::webUiOverrideAllowed(httpd_req_t *request) {
  char clientId[WEB_UI_CLIENT_ID_CAPACITY] = {};
  if (!readWebUiClientId(request, clientId, sizeof(clientId))) return false;
  const uint32_t now = millis();
  bool allowed = false;
  dataMux_.lock();
  if (webUiOverrideActive_ && activeWebUiClientId_[0] != '\0' &&
      strcmp(activeWebUiClientId_, clientId) == 0) {
    if (static_cast<int32_t>(now - webUiOverrideUntilMs_) >= 0) {
      webUiOverrideActive_ = false;
      webUiOverrideUntilMs_ = 0;
    } else {
      allowed = true;
    }
  }
  dataMux_.unlock();
  memset(clientId, 0, sizeof(clientId));
  return allowed;
}

uint32_t ShotStopperNetwork::webUiOverrideRemainingMs(httpd_req_t *request) {
  char clientId[WEB_UI_CLIENT_ID_CAPACITY] = {};
  if (!readWebUiClientId(request, clientId, sizeof(clientId))) return 0;
  const uint32_t now = millis();
  uint32_t remaining = 0;
  dataMux_.lock();
  if (webUiOverrideActive_ && activeWebUiClientId_[0] != '\0' &&
      strcmp(activeWebUiClientId_, clientId) == 0 &&
      static_cast<int32_t>(now - webUiOverrideUntilMs_) < 0) {
    remaining = webUiOverrideUntilMs_ - now;
  }
  dataMux_.unlock();
  memset(clientId, 0, sizeof(clientId));
  return remaining;
}

bool ShotStopperNetwork::webUiConfigurationAllowed(
    httpd_req_t *request, const ControlGateSnapshot &status) {
  if (controlAllowsConfiguration(status)) {
    return true;
  }
  return webUiOverrideAllowed(request);
}

bool ShotStopperNetwork::historyMutationAllowed(
    httpd_req_t *request, const ControlGateSnapshot &status) {
  if (!controlAllowsHistoryMutation(status)) {
    return false;
  }
  return webUiConfigurationAllowed(request, status);
}

void ShotStopperNetwork::clearAdminUnlock() {
  dataMux_.lock();
  adminUnlocked_ = false;
  adminUnlockClientId_[0] = '\0';
  adminUnlockUntilMs_ = 0;
  dataMux_.unlock();
}

void ShotStopperNetwork::grantAdminUnlock(const char *clientId, uint32_t now) {
  if (clientId == nullptr) {
    return;
  }
  dataMux_.lock();
  memcpy(adminUnlockClientId_, clientId, sizeof(adminUnlockClientId_));
  adminUnlocked_ = true;
  adminUnlockUntilMs_ = now + ADMIN_UNLOCK_IDLE_MS;
  adminUnlockFailures_ = 0;
  adminUnlockCooldownUntilMs_ = 0;
  dataMux_.unlock();
}

void ShotStopperNetwork::touchAdminUnlock() {
  const uint32_t now = millis();
  dataMux_.lock();
  if (adminUnlocked_) {
    adminUnlockUntilMs_ = now + ADMIN_UNLOCK_IDLE_MS;
  }
  dataMux_.unlock();
}

bool ShotStopperNetwork::adminUnlockAllowed(httpd_req_t *request) {
#if SHOT_STOPPER_DEVELOPMENT == 1
  (void)request;
  return true;
#else
  char clientId[WEB_UI_CLIENT_ID_CAPACITY] = {};
  if (!readWebUiClientId(request, clientId, sizeof(clientId))) {
    return false;
  }
  const uint32_t now = millis();
  bool allowed = false;
  dataMux_.lock();
  if (adminUnlocked_ && adminUnlockClientId_[0] != '\0' &&
      strcmp(adminUnlockClientId_, clientId) == 0) {
    if (static_cast<int32_t>(now - adminUnlockUntilMs_) >= 0) {
      adminUnlocked_ = false;
      adminUnlockClientId_[0] = '\0';
      adminUnlockUntilMs_ = 0;
    } else {
      allowed = true;
    }
  }
  dataMux_.unlock();
  memset(clientId, 0, sizeof(clientId));
  return allowed;
#endif
}

bool ShotStopperNetwork::requireAdminUnlock(httpd_req_t *request) {
  if (!adminUnlockAllowed(request)) {
    sendError(request, STATUS_UNAUTHORIZED, "ADMIN_LOCKED",
              "Unlock administration with the device password first.");
    return false;
  }
  touchAdminUnlock();
  return true;
}

bool ShotStopperNetwork::diagnosticPageEnabled() {
  bool visible = false;
  dataMux_.lock();
  visible = settings_.runtime.showDiagnosticPage;
  dataMux_.unlock();
  return visible;
}

esp_err_t ShotStopperNetwork::adminUnlockHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  char password[WIFI_PASSWORD_CAPACITY] = {};
  cJSON *root = parseJsonDocument(self.workBuf_->requestBody);
  static const char *const fields[] = {"password"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "password", password, sizeof(password), false);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    memset(password, 0, sizeof(password));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_DEVICE_PASSWORD",
                     "password is required.");
  }

  char clientId[WEB_UI_CLIENT_ID_CAPACITY] = {};
  if (!readWebUiClientId(request, clientId, sizeof(clientId))) {
    memset(password, 0, sizeof(password));
    return sendError(request, STATUS_BAD_REQUEST, "UI_CLIENT_INVALID",
                     "X-WebUI-Client must be a 16-24 character lowercase hex id.");
  }

  const uint32_t now = millis();
  bool coolingDown = false;
  self.dataMux_.lock();
  coolingDown = self.adminUnlockCooldownUntilMs_ != 0 &&
                static_cast<int32_t>(now - self.adminUnlockCooldownUntilMs_) <
                    0;
  self.dataMux_.unlock();
  if (coolingDown) {
    memset(password, 0, sizeof(password));
    memset(clientId, 0, sizeof(clientId));
    return sendError(request, STATUS_TOO_MANY, "ADMIN_UNLOCK_COOLDOWN",
                     "Too many failed unlock attempts. Try again shortly.");
  }

  char expected[WIFI_PASSWORD_CAPACITY] = {};
  self.dataMux_.lock();
  memcpy(expected, self.settings_.devicePassword, sizeof(expected));
  self.dataMux_.unlock();
  const bool matches = secretsMatch(password, expected);
  memset(password, 0, sizeof(password));
  memset(expected, 0, sizeof(expected));
  if (!matches) {
    uint8_t failures = 0;
    self.dataMux_.lock();
    if (self.adminUnlockFailures_ < 255) {
      self.adminUnlockFailures_ =
          static_cast<uint8_t>(self.adminUnlockFailures_ + 1);
    }
    failures = self.adminUnlockFailures_;
    if (failures >= ADMIN_UNLOCK_FAILURES_BEFORE_COOLDOWN) {
      self.adminUnlockCooldownUntilMs_ = now + ADMIN_UNLOCK_COOLDOWN_MS;
      self.adminUnlockFailures_ = 0;
    }
    self.dataMux_.unlock();
    memset(clientId, 0, sizeof(clientId));
    return sendError(request, STATUS_UNAUTHORIZED, "DEVICE_PASSWORD_INVALID",
                     "Device password is incorrect.");
  }

  self.grantAdminUnlock(clientId, now);
  memset(clientId, 0, sizeof(clientId));
  return sendJson(request, STATUS_OK, "{\"unlocked\":true}");
}

esp_err_t ShotStopperNetwork::adminLockHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  self.unlockJsonBody();
  self.clearAdminUnlock();
  return sendJson(request, STATUS_OK, "{\"unlocked\":false}");
}

bool ShotStopperNetwork::requireActiveWebUiClient(httpd_req_t *request) {
  char clientId[WEB_UI_CLIENT_ID_CAPACITY] = {};
  if (!readWebUiClientId(request, clientId, sizeof(clientId))) {
    sendError(request, STATUS_CONFLICT, "UI_CLAIM_REQUIRED",
              "Claim WebUI control before using this API.");
    return false;
  }
  bool active = false;
  dataMux_.lock();
  active = activeWebUiClientId_[0] != '\0' &&
           strcmp(activeWebUiClientId_, clientId) == 0;
  dataMux_.unlock();
  memset(clientId, 0, sizeof(clientId));
  if (!active) {
    sendError(request, STATUS_CONFLICT, "UI_TAKEN_OVER",
              "This WebUI window is inactive. Reactivate to continue.");
    return false;
  }
  return true;
}

esp_err_t ShotStopperNetwork::ownedApiHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireActiveWebUiClient(request)) {
    return ESP_OK;
  }
  if (apiUriMatches(request->uri, "/api/v1/status/home") ||
      apiUriMatches(request->uri, "/api/v1/status/settings") ||
      apiUriMatches(request->uri, "/api/v1/status/admin") ||
      apiUriMatches(request->uri, "/api/v1/status/diagnostic")) {
    return statusHandler(request);
  }
  if (apiUriMatches(request->uri, "/api/v1/debug/export")) {
    return debugExportHandler(request);
  }
  if (apiUriMatches(request->uri, "/api/v1/log")) return logHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/shots")) return shotsHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/shots/clear")) return shotsClearHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/shots/delete")) return shotsDeleteHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/shots/rate")) return shotsRateHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/last-shot/clear")) return lastShotClearHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/time/sync")) return timeSyncHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/config")) return configHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/webhooks")) return webhookHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/bullseye/test")) return bullseyeTestHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/scale/preferred/clear")) return preferredScaleClearHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/scale/preferred/select")) return preferredScaleSelectHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/presets")) return presetsHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/calibration/reset")) return resetCalibrationHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/calibration/reset-guard-samples")) return resetGuardSamplesHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/control/paddle")) return paddleHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/control/rinse")) return rinseHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/control/stop")) return stopHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/control/force-pulse")) return forcePulseHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/control/state-override")) return stateOverrideHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/control/restart")) return restartHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/diagnostic/reset-history")) return clearResetHistoryHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/factory-reset")) return factoryResetHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/network/scan")) {
    return request->method == HTTP_GET ? wifiScanStatusHandler(request)
                                       : wifiScanStartHandler(request);
  }
  if (apiUriMatches(request->uri, "/api/v1/network")) return networkHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/device/password")) return devicePasswordHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/admin/unlock")) return adminUnlockHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/admin/lock")) return adminLockHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/admin/ble-compat")) return bleCompatHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/diagnostic/profiler")) return taskProfilerHandler(request);
  return sendError(request, STATUS_NOT_FOUND, "NOT_FOUND", "Unknown API route.");
}

bool ShotStopperNetwork::requireJsonContentType(httpd_req_t *request) {
  const size_t length =
      httpd_req_get_hdr_value_len(request, "Content-Type");
  if (length == 0 || length >= 64) {
    return false;
  }
  char contentType[64] = {};
  if (httpd_req_get_hdr_value_str(request, "Content-Type", contentType,
                                   sizeof(contentType)) != ESP_OK) {
    return false;
  }
  const size_t jsonLength = strlen(JSON_CONTENT_TYPE);
  if (strncmp(contentType, JSON_CONTENT_TYPE, jsonLength) != 0) {
    return false;
  }
  const char suffix = contentType[jsonLength];
  return suffix == '\0' || suffix == ';' || suffix == ' ' || suffix == '\t';
}

bool ShotStopperNetwork::readJsonBody(
    httpd_req_t *request, char body[REQUEST_BODY_CAPACITY]) {
  if (!requireJsonContentType(request) || request->content_len <= 0 ||
      request->content_len >= REQUEST_BODY_CAPACITY) {
    return false;
  }
  size_t received = 0;
  while (received < static_cast<size_t>(request->content_len)) {
    const int result = httpd_req_recv(
        request, body + received,
        static_cast<size_t>(request->content_len) - received);
    if (result <= 0) {
      return false;
    }
    received += static_cast<size_t>(result);
  }
  body[received] = '\0';
  return true;
}

static bool ifNoneMatchEquals(httpd_req_t *request, const char *etag) {
  const size_t length = httpd_req_get_hdr_value_len(request, "If-None-Match");
  if (length == 0 || length >= IF_NONE_MATCH_CAPACITY) {
    return false;
  }
  char value[IF_NONE_MATCH_CAPACITY] = {};
  if (httpd_req_get_hdr_value_str(request, "If-None-Match", value,
                                  sizeof(value)) != ESP_OK) {
    return false;
  }
  return strcmp(value, etag) == 0;
}

static esp_err_t serveImmutableGzip(httpd_req_t *request, const char *contentType,
                                    const uint8_t *data, size_t length) {
  // WEB_UI_ETAG is bake-time (FW_VERSION + asset tag); immutable ?v= busts
  // after reflashes without snprintf on each request.
  if (ifNoneMatchEquals(request, WEB_UI_ETAG)) {
    httpd_resp_set_status(request, STATUS_NOT_MODIFIED);
    httpd_resp_set_hdr(request, "Cache-Control",
                       "public, max-age=31536000, immutable");
    httpd_resp_set_hdr(request, "ETag", WEB_UI_ETAG);
    httpd_resp_set_hdr(request, "Connection", "close");
    return httpd_resp_send(request, nullptr, 0);
  }
  httpd_resp_set_type(request, contentType);
  httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(request, "Cache-Control",
                     "public, max-age=31536000, immutable");
  httpd_resp_set_hdr(request, "ETag", WEB_UI_ETAG);
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(request, "Connection", "close");
  return sendCopiedBody(request, data, length);
}

esp_err_t ShotStopperNetwork::rootHandler(httpd_req_t *request) {
  if (ifNoneMatchEquals(request, WEB_UI_ETAG)) {
    httpd_resp_set_status(request, STATUS_NOT_MODIFIED);
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(request, "ETag", WEB_UI_ETAG);
    httpd_resp_set_hdr(request, "Connection", "close");
    return httpd_resp_send(request, nullptr, 0);
  }
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(request, "ETag", WEB_UI_ETAG);
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(
      request, "Content-Security-Policy",
      "default-src 'self'; script-src 'self'; style-src 'self'; "
      "connect-src 'self'; frame-ancestors 'none'");
  httpd_resp_set_hdr(request, "Connection", "close");
  return sendCopiedBody(request, SHOT_STOPPER_WEB_UI_GZIP,
                        SHOT_STOPPER_WEB_UI_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::jsHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "application/javascript; charset=utf-8",
                            SHOT_STOPPER_WEB_JS_GZIP,
                            SHOT_STOPPER_WEB_JS_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::cssHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "text/css; charset=utf-8",
                            SHOT_STOPPER_WEB_CSS_GZIP,
                            SHOT_STOPPER_WEB_CSS_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::runtimeJsHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "application/javascript; charset=utf-8",
                            SHOT_STOPPER_WEB_RUNTIME_GZIP,
                            SHOT_STOPPER_WEB_RUNTIME_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::otaImageJsHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "application/javascript; charset=utf-8", SHOT_STOPPER_WEB_OTA_IMAGE_GZIP, SHOT_STOPPER_WEB_OTA_IMAGE_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::secondaryJsHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "application/javascript; charset=utf-8",
                            SHOT_STOPPER_WEB_SECONDARY_GZIP,
                            SHOT_STOPPER_WEB_SECONDARY_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::partialStatsHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "text/html; charset=utf-8",
                            SHOT_STOPPER_WEB_PARTIAL_STATS_GZIP,
                            SHOT_STOPPER_WEB_PARTIAL_STATS_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::partialDiagnosticHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  // With the page disabled the partial must not exist for any visitor; the
  // SPA shell keeps serving so /diagnostic can bounce to Home.
  if (!self.diagnosticPageEnabled()) {
    return sendError(request, "404 Not Found", "DIAGNOSTIC_DISABLED",
                     "Diagnostic page is disabled by the administrator.");
  }
  return serveImmutableGzip(request, "text/html; charset=utf-8",
                            SHOT_STOPPER_WEB_PARTIAL_DIAGNOSTIC_GZIP,
                            SHOT_STOPPER_WEB_PARTIAL_DIAGNOSTIC_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::partialSettingsHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "text/html; charset=utf-8",
                            SHOT_STOPPER_WEB_PARTIAL_SETTINGS_GZIP,
                            SHOT_STOPPER_WEB_PARTIAL_SETTINGS_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::partialAdminHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "text/html; charset=utf-8",
                            SHOT_STOPPER_WEB_PARTIAL_ADMIN_GZIP,
                            SHOT_STOPPER_WEB_PARTIAL_ADMIN_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::viewSettingsHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "application/javascript; charset=utf-8",
                            SHOT_STOPPER_WEB_VIEW_SETTINGS_GZIP,
                            SHOT_STOPPER_WEB_VIEW_SETTINGS_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::browserIconHandler(httpd_req_t *request) {
  // Safari/iOS probe these paths on every visit. A registered handler avoids
  // the IDF "URI not found" WARN and the 302-to-/ HTML bounce.
  httpd_resp_set_status(request, STATUS_NO_CONTENT);
  httpd_resp_set_hdr(request, "Cache-Control",
                     "public, max-age=31536000, immutable");
  httpd_resp_set_hdr(request, "Connection", "close");
  return httpd_resp_send(request, nullptr, 0);
}

esp_err_t ShotStopperNetwork::notFoundHandler(httpd_req_t *request,
                                              httpd_err_code_t) {
  // Keep API 404s as JSON; browser paths bounce to Home.
  if (strncmp(request->uri, "/api/", 5) == 0) {
    return sendError(request, STATUS_NOT_FOUND, "NOT_FOUND",
                     "The requested resource was not found.");
  }
  httpd_resp_set_status(request, "302 Found");
  httpd_resp_set_hdr(request, "Location", "/");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, nullptr, 0);
}


const char *ShotStopperNetwork::stateLabel(StopperState state) {
  switch (state) {
    case StopperState::REQUIRES_OFF: return "Paddle release required";
    case StopperState::READY: return "Ready";
    case StopperState::BREW: return "Automatic brew";
    case StopperState::RINSE: return "Rinse in progress";
    case StopperState::MANUAL_NO_SCALE:
      return "Manual shot without scale";
  }
  return "Unknown";
}

const char *ShotStopperNetwork::controlSourceName(ControlSource source) {
  switch (source) {
    case ControlSource::NONE: return "none";
    case ControlSource::PHYSICAL: return "physical";
    case ControlSource::WEB: return "web";
  }
  return "unknown";
}

const char *activeCycleShotTypeLabel(const ControlStatusSnapshot &control) {
  if (!control.activeCycle) {
    return "idle";
  }
  if (control.state == StopperState::RINSE) {
    return "rinse";
  }
  return shotLogTypeName(shotLogTypeFromCycle(
      control.state, control.cycleStartedWithScale, control.cycleTimerOnly,
      control.cycleAutomaticBrew));
}

const char *ShotStopperNetwork::endReasonName(EndReason reason) {
  switch (reason) {
    case EndReason::NONE: return "NONE";
    case EndReason::ACTIVATOR: return "ACTIVATOR";
    case EndReason::SCALE_THRESHOLD: return "SCALE_THRESHOLD";
    case EndReason::WEIGHT_ANOMALY: return "WEIGHT_ANOMALY";
    case EndReason::GLOBAL_LIMIT: return "GLOBAL_LIMIT";
    case EndReason::CONFIGURED_WALL_LIMIT:
      return "CONFIGURED_WALL_LIMIT";
    case EndReason::SHORT_SHOT: return "SHORT_SHOT";
    case EndReason::RINSE_COMPLETE: return "RINSE_COMPLETE";
    case EndReason::WEB_STOP: return "WEB_STOP";
    case EndReason::PHYSICAL_OVERRIDE: return "PHYSICAL_OVERRIDE";
    case EndReason::WEB_HEARTBEAT_TIMEOUT:
      return "WEB_HEARTBEAT_TIMEOUT";
    case EndReason::RELAY_SAFETY_FAILURE: return "RELAY_SAFETY_FAILURE";
    case EndReason::FAST_EXTRACTION_MAX_WEIGHT:
      return "FAST_EXTRACTION_MAX_WEIGHT";
    case EndReason::FAST_EXTRACTION_MIN_TIME:
      return "FAST_EXTRACTION_MIN_TIME";
    case EndReason::SLOW_EXTRACTION_MAX_TIME:
      return "SLOW_EXTRACTION_MAX_TIME";
    case EndReason::SLOW_EXTRACTION_MIN_WEIGHT:
      return "SLOW_EXTRACTION_MIN_WEIGHT";
    case EndReason::AUTO_TO_MANUAL_GUARD:
      return "AUTO_TO_MANUAL_GUARD";
    case EndReason::CUP_REMOVED:
      return "CUP_REMOVED";
    case EndReason::UNCONFIRMED_START:
      return "UNCONFIRMED_START";
  }
  return "UNKNOWN";
}

const char *ShotStopperNetwork::staStateName(StaState state) {
  switch (state) {
    case StaState::NOT_CONFIGURED: return "NOT_CONFIGURED";
    case StaState::CONNECTING: return "CONNECTING";
    case StaState::CONNECTED: return "CONNECTED";
    case StaState::FAILED: return "FAILED";
    case StaState::DISCONNECTED: return "DISCONNECTED";
  }
  return "UNKNOWN";
}
