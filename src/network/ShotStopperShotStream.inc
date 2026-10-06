// httpd owns framing, session contexts and serialization. The existing priority-1
// network task queues at most one dispatch; it never sends or borrows JSON scratch.
namespace {
constexpr uint32_t kShotStreamBindMs = 5000, kShotStreamRetainMs = 30000;
// Live shots keep the scale's 10 Hz; idle Home only needs edge pickup.
constexpr uint32_t kShotStreamLiveMs = 100, kShotStreamIdleMs = 250;
constexpr size_t kShotStreamRequestBytes = 160;

bool shotStreamOriginMatches(const char *origin, const char *host) {
  if (!host || !host[0]) return false;
  char accepted[DEVICE_NAME_CAPACITY + 24];
  snprintf(accepted, sizeof(accepted), "http://%s", host);
  if (strcmp(origin, accepted) == 0) return true;
  snprintf(accepted, sizeof(accepted), "http://%s:80", host);
  return strcmp(origin, accepted) == 0;
}

uint32_t shotStreamCurveHash(const ShotCurveRecord &curve, uint16_t count) {
  uint32_t hash = crc32Update(0xFFFFFFFFU,
      reinterpret_cast<const uint8_t *>(curve.weightCg), count * sizeof(int16_t));
  hash = crc32Update(hash, reinterpret_cast<const uint8_t *>(curve.atMs),
                    count * sizeof(uint16_t));
  for (uint16_t i = 0; i < count; ++i) {
    const uint8_t split = shotCurveBreakBefore(curve, i) ? 1 : 0;
    hash = crc32Update(hash, &split, 1);
  }
  return hash;
}

// IDF's WS sender calls send_fn once per header/payload and accepts short writes.
// Complete each write or fail within 100 ms, including slow-client backpressure.
int shotStreamSend(httpd_handle_t, int fd, const char *data, size_t length, int flags) {
  const uint32_t started = millis();
  size_t done = 0;
  while (done < length) {
    const uint32_t age = millis() - started;
    if (age >= 100) return -1;
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(fd, &writable);
    timeval wait = {0, static_cast<suseconds_t>((100 - age) * 1000)};
    if (select(fd + 1, nullptr, &writable, nullptr, &wait) <= 0) return -1;
    const int sent = send(fd, data + done, length - done, flags | MSG_DONTWAIT);
    if (sent > 0) done += static_cast<size_t>(sent);
    else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return -1;
  }
  return static_cast<int>(done);
}
}  // namespace

#include "ShotStopperHomeStream.inc"

esp_err_t ShotStopperNetwork::shotStreamHandshake(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  char origin[DEVICE_NAME_CAPACITY + 24] = {}, local[DEVICE_NAME_CAPACITY + 8] = {};
  const size_t length = httpd_req_get_hdr_value_len(request, "Origin");
  if (length == 0 || length >= sizeof(origin) ||
      httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof(origin)) != ESP_OK)
    return ESP_FAIL;
  const NetworkStatusSnapshot network = self.snapshot();
  snprintf(local, sizeof(local), "%s.local", network.mdnsHost);
  if (!shotStreamOriginMatches(origin, network.staIp) &&
      !shotStreamOriginMatches(origin, network.apIp) &&
      !shotStreamOriginMatches(origin, local)) return ESP_FAIL;
  TaskLockGuard lock(self.dataMux_);
  for (auto &session : self.shotStreams_) {
    if (session.fd != -1) continue;
    session = ShotStreamSession{};
    session.fd = httpd_req_to_sockfd(request);
    session.openedAtMs = millis();
    request->sess_ctx = &session;
    request->free_ctx = shotStreamFree;
    // Bound incomplete incoming frames independently of ordinary HTTP/OTA.
    const timeval timeout = {0, 100000};
    setsockopt(session.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return ESP_OK;
  }
  return ESP_FAIL;
}

void ShotStopperNetwork::shotStreamFree(void *context) {
  if (!instance_ || !context) return;
  TaskLockGuard lock(instance_->dataMux_);
  *static_cast<ShotStreamSession *>(context) = ShotStreamSession{};
}

esp_err_t ShotStopperNetwork::shotStreamHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  auto *session = static_cast<ShotStreamSession *>(request->sess_ctx);
  if (!session) return ESP_FAIL;
  httpd_ws_frame_t frame = {};
  char body[kShotStreamRequestBytes + 1] = {};
  if (httpd_ws_recv_frame(request, &frame, 0) != ESP_OK ||
      frame.type != HTTPD_WS_TYPE_TEXT || !frame.final ||
      frame.len == 0 || frame.len > kShotStreamRequestBytes) return ESP_FAIL;
  frame.payload = reinterpret_cast<uint8_t *>(body);
  if (httpd_ws_recv_frame(request, &frame, frame.len) != ESP_OK ||
      memchr(body, '\0', frame.len)) return ESP_FAIL;
  const char *end = nullptr;
  cJSON *message = cJSON_ParseWithOpts(body, &end, true);
  if (!message) return ESP_FAIL;
  const cJSON *op = cJSON_GetObjectItemCaseSensitive(message, "op");
  const cJSON *client = cJSON_GetObjectItemCaseSensitive(message, "client");
  bool accepted = false;
  bool activity = false;
  int superseded = -1;
  {
    TaskLockGuard lock(self.dataMux_);
    if (cJSON_IsString(op) && session->bound &&
        strcmp(op->valuestring, "activity") == 0 &&
        strcmp(session->clientId, self.activeWebUiClientId_) == 0) {
      const cJSON *seconds = cJSON_GetObjectItemCaseSensitive(message, "seconds");
      if (cJSON_IsNumber(seconds) && seconds->valuedouble >= 0 &&
          seconds->valuedouble <= 30 && seconds->valuedouble == seconds->valueint) {
        notePowerWebActivity(millis(), static_cast<uint32_t>(seconds->valueint));
        accepted = activity = true;
      }
    } else if (cJSON_IsString(op) && session->bound &&
        strcmp(op->valuestring, "resync") == 0 &&
        strcmp(session->clientId, self.activeWebUiClientId_) == 0) {
      session->resync = true;
      session->homeResync = true;
      accepted = true;
    } else if (cJSON_IsString(op) && session->bound &&
               strcmp(op->valuestring, "diagnostic") == 0 &&
               strcmp(session->clientId, self.activeWebUiClientId_) == 0) {
      // View-scoped telemetry toggle: the client subscribes only while the
      // Diagnostic page is shown and unsubscribes on every navigation.
      const cJSON *on = cJSON_GetObjectItemCaseSensitive(message, "on");
      if (cJSON_IsBool(on)) {
        session->diagnostic = cJSON_IsTrue(on);
        session->diagResync = session->diagnostic;
        accepted = true;
      }
    } else if (cJSON_IsString(op) && !session->bound &&
               strcmp(op->valuestring, "bind") == 0 && cJSON_IsString(client) &&
               self.activeWebUiClientId_[0] &&
               strcmp(client->valuestring, self.activeWebUiClientId_) == 0 &&
               millis() - session->openedAtMs < kShotStreamBindMs) {
      copyCString(session->clientId, sizeof(session->clientId), client->valuestring);
      const cJSON *cycle = cJSON_GetObjectItemCaseSensitive(message, "cycle");
      const cJSON *boot = cJSON_GetObjectItemCaseSensitive(message, "boot");
      if (cJSON_IsNumber(cycle) && cycle->valuedouble > 0 &&
          cycle->valuedouble <= UINT32_MAX && floor(cycle->valuedouble) == cycle->valuedouble &&
          cJSON_IsNumber(boot) && boot->valuedouble >= 0 &&
          boot->valuedouble <= UINT32_MAX && floor(boot->valuedouble) == boot->valuedouble) {
        session->observedCycle = static_cast<uint32_t>(cycle->valuedouble);
        session->observedBoot = static_cast<uint32_t>(boot->valuedouble);
      }
      for (auto &other : self.shotStreams_) {
        if (&other != session && other.bound) {
          superseded = other.fd;
          other.bound = false;
          other.openedAtMs = millis() - kShotStreamBindMs;
        }
      }
      session->bound = true;
      session->boundAtMs = millis();
      accepted = true;
    }
  }
  cJSON_Delete(message);
  memset(body, 0, sizeof(body));
  if (!accepted) return ESP_FAIL;
  if (activity) {
    uint8_t alive[] = "{\"v\":1,\"type\":\"alive\"}";
    httpd_ws_frame_t reply = {};
    reply.type = HTTPD_WS_TYPE_TEXT;
    reply.payload = alive;
    reply.len = sizeof(alive) - 1;
    return httpd_ws_send_frame_async(request->handle, session->fd, &reply);
  }
  if (superseded >= 0) httpd_sess_trigger_close(request->handle, superseded);
  httpd_sess_set_send_override(request->handle, session->fd, shotStreamSend);
  self.requestPendingNetworkConfirm();
  return ESP_OK;
}

void ShotStopperNetwork::serviceShotStream(uint32_t now) {
  if (!server_) return;
  const uint32_t cadence = controlCriticalRfActive_.load(std::memory_order_acquire)
                                ? kShotStreamLiveMs : kShotStreamIdleMs;
  if (!shotStreamUrgent_.load(std::memory_order_acquire) &&
      now - shotStreamDispatchAtMs_ < cadence) return;
  bool occupied = false;
  {
    TaskLockGuard lock(dataMux_);
    for (const auto &session : shotStreams_) occupied |= session.fd >= 0;
  }
  if (!occupied || shotStreamWorkPending_.exchange(true, std::memory_order_acq_rel)) return;
  shotStreamUrgent_.exchange(false, std::memory_order_acq_rel);
  shotStreamDispatchAtMs_ = now;
  if (httpd_queue_work(server_, shotStreamDispatch, this) != ESP_OK) {
    shotStreamUrgent_.store(true, std::memory_order_release);
    shotStreamWorkPending_.store(false, std::memory_order_release);
  }
}

void ShotStopperNetwork::shotStreamDispatch(void *context) {
  auto &self = *static_cast<ShotStopperNetwork *>(context);
  for (auto &session : self.shotStreams_) {
    if (session.fd < 0) continue;
    bool allowed;
    {
      TaskLockGuard lock(self.dataMux_);
      allowed = session.bound
          ? strcmp(session.clientId, self.activeWebUiClientId_) == 0
          : millis() - session.openedAtMs < kShotStreamBindMs;
    }
    if (!allowed) {
      if (session.bound) {
        uint8_t code[] = {0x0f, 0xa1};  // 4001: ownership taken over.
        httpd_ws_frame_t close = {};
        close.type = HTTPD_WS_TYPE_CLOSE;
        close.payload = code;
        close.len = sizeof(code);
        (void)httpd_ws_send_frame_async(self.server_, session.fd, &close);
      }
      httpd_sess_trigger_close(self.server_, session.fd);
    }
    else if (session.bound) {
      httpd_sess_update_lru_counter(self.server_, session.fd);
      self.sendShotStream(session);
    }
  }
  self.shotStreamWorkPending_.store(false, std::memory_order_release);
}

void ShotStopperNetwork::sendShotStream(ShotStreamSession &session) {
  // httpd serialization is exclusive. Try rather than wait behind Network's
  // occasional settings work; the next dispatch recovers a complete view.
  if (xSemaphoreTake(statusResponseMux_.get(), 0) != pdTRUE) {
    shotStreamUrgent_.store(true, std::memory_order_release);
    return;
  }
  callbacks_.refreshControlStatus();
  ControlStatusSnapshot &control = workBuf_->control;
  loadControlStatus(control);
  // The RF notification can precede the committed presentation snapshot.
  if ((control.activeCycle || control.relayClosed) !=
      controlCriticalRfActive_.load(std::memory_order_acquire))
    shotStreamUrgent_.store(true, std::memory_order_release);
  ShotCurveRecord &curve = workBuf_->serializedCurve;
  ShotLogRecord latest = {};
  const bool eligible = callbacks_.copyHomeShot &&
      callbacks_.copyHomeShot(latest, workBuf_->homeCurve);
  if (!sendHomeStream(session, control, eligible ? &latest : nullptr)) {
    unlockWorkBuf();
    httpd_sess_trigger_close(server_, session.fd);
    return;
  }
  if (session.diagnostic && !sendDiagnosticStream(session, control)) {
    unlockWorkBuf();
    httpd_sess_trigger_close(server_, session.fd);
    return;
  }
  const PersistedLastShot &cycle = control.homeCycle;
  const bool live = control.activeCycle || control.homePending;
  if (live) {
    session.observedCycle = cycle.cycleId;
    session.observedBoot = control.bootId;
  }
  const uint32_t age = millis() - control.homeResolvedAtMs;
  const bool retained = !live && cycle.valid && cycle.shotLogId == 0 &&
      session.observedBoot == control.bootId && session.observedCycle == cycle.cycleId &&
      age < kShotStreamRetainMs;
  PersistedLastShot card = {};
  const char *phase = "idle";
  if (live || retained) {
    card = cycle;
    phase = control.activeCycle ? "active" : control.homePending ? "pending" : "transient";
    shotCurveRecordFromStatusFields(curve, control.shotCurveCount, control.shotCurveTruncated,
        control.shotCurveFirstDropMs, control.shotCurveFirstDropCg,
        control.shotCurveExtendedMs, control.shotCurveExtendedCg,
        control.shotCurveAtmMs, control.shotCurveAtmCg, control.shotCurveAtmClearedMs,
        control.shotCurveEndedMs, control.shotCurveEndedCg, control.shotCurveWeightCg,
        SHOT_CURVE_MAX_POINTS, control.shotCurveAtMs, control.shotCurveBreakBefore);
  } else if (eligible) {
    const auto &linked = control.lastShot.shotLogId == latest.id
        ? control.lastShot : control.lastGoodShot;
    card = shotLogProjectLastShot(latest, linked);
    curve = workBuf_->homeCurve;
  } else resetShotCurveRecord(curve);
  const bool savePending = eligible && callbacks_.shotLogSavePending && callbacks_.shotLogSavePending();
  char preset[sizeof(card.presetName) * 6], scale[sizeof(card.scaleName) * 6];
  escapeJsonString(card.presetName, preset, sizeof(preset));
  escapeJsonString(card.scaleName, scale, sizeof(scale));
  char weight[32] = "null", flow[32] = "null", drop[16] = "null", tare[16] = "null";
  if (card.weightValid && std::isfinite(card.currentWeightG))
    snprintf(weight, sizeof(weight), "%.2f", static_cast<double>(card.currentWeightG));
  if (card.averageFlowValid && std::isfinite(card.averageFlowGps))
    snprintf(flow, sizeof(flow), "%.2f", static_cast<double>(card.averageFlowGps));
  const bool canonical = !live && !retained && eligible;
  if (canonical ? latest.firstDropDs != SHOT_LOG_METRIC_MISSING
                : card.firstDropElapsedMs != 0 || shotCurveEventPresent(curve.firstDrop))
    snprintf(drop, sizeof(drop), "%lu", static_cast<unsigned long>(card.firstDropElapsedMs));
  if (canonical ? latest.tareAtDs != SHOT_LOG_METRIC_MISSING
                : card.tareElapsedMs != 0)
    snprintf(tare, sizeof(tare), "%lu", static_cast<unsigned long>(card.tareElapsedMs));
  char *output = workBuf_->jsonItem;
  const int cardBytes = snprintf(output, NetworkWorkBuf::kJsonItem,
      "\"phase\":\"%s\",\"card\":{\"valid\":%s,\"live\":%s,\"weight\":%s,"
      "\"weightValid\":%s,\"goal\":%u,\"elapsedMs\":%lu,\"firstDropMs\":%s,"
      "\"tareMs\":%s,\"presetName\":\"%s\",\"scaleName\":\"%s\","
      "\"scaleAvailable\":%s,\"shotType\":\"%s\",\"endReason\":\"%s\",\"averageFlowGps\":%s,"
      "\"extractionExtended\":%s,\"slowExtractionExtended\":%s,"
      "\"rating\":%u,\"savePending\":%s}",
      phase, card.valid ? "true" : "false", control.activeCycle ? "true" : "false",
      weight, card.weightValid ? "true" : "false", card.goalWeightG,
      static_cast<unsigned long>(card.durationMs), drop, tare, preset, scale,
      card.scaleAvailable ? "true" : "false",
      lastShotTypeName(static_cast<LastShotType>(card.shotType)),
      !live && !retained && eligible ? shotLogStopDetailName(static_cast<ShotLogStopDetail>(latest.stopDetail))
                                  : shotLogStopDetailName(shotLogStopDetailFromEndReason(
                                        card.endReason, card.fastExtractionGuardEnabled, card.extractionExtended)),
      flow, card.extractionExtended ? "true" : "false", card.slowExtractionExtended ? "true" : "false",
      card.rating, savePending ? "true" : "false");
  if (cardBytes <= 0 || static_cast<size_t>(cardBytes) >= NetworkWorkBuf::kJsonItem) {
    unlockWorkBuf();
    return;
  }
  const uint32_t markers = crc32Update(0xFFFFFFFFU,
      reinterpret_cast<const uint8_t *>(&curve.atmClearedMs),
      offsetof(ShotCurveRecord, weightCg) - offsetof(ShotCurveRecord, atmClearedMs));
  const uint32_t prefix = shotStreamCurveHash(curve, curve.count);
  uint32_t fingerprint = crc32Update(prefix, reinterpret_cast<const uint8_t *>(output), cardBytes);
  fingerprint = crc32Update(fingerprint, reinterpret_cast<const uint8_t *>(&markers), sizeof(markers));
  fingerprint ^= curve.truncated ? 1U : 0U;
  fingerprint ^= control.snapshotStale ? 2U : 0U;
  const bool replace = session.resync || session.boot != control.bootId ||
      session.cycle != card.cycleId || session.shot != card.shotLogId ||
      session.cursor > curve.count || session.markersHash != markers ||
      session.prefixHash != shotStreamCurveHash(curve, session.cursor);
  if (!replace && session.fingerprint == fingerprint) {
    unlockWorkBuf();
    return;
  }
  const uint16_t base = replace ? 0 : session.cursor;
  const bool formatted = formatShotCurveJsonBody(workBuf_->curveJson, SHOT_CURVE_JSON_CAPACITY, curve, base);
  // Move scalar body behind its envelope in the same bounded workspace.
  char envelope[384];
  const int envelopeBytes = snprintf(envelope, sizeof(envelope),
      "{\"v\":1,\"boot\":%lu,\"seq\":%lu,\"base\":%lu,\"revision\":%lu,"
      "\"snapshot\":%s,\"cycle\":%lu,\"shotId\":%lu,\"latestShotId\":%lu,"
      "\"remainingMs\":%lu,\"stale\":%s,\"curveBase\":%u,\"cursor\":%u,",
      static_cast<unsigned long>(control.bootId), static_cast<unsigned long>(session.sequence + 1),
      static_cast<unsigned long>(session.sequence), static_cast<unsigned long>(control.snapshotVersion),
      replace ? "true" : "false", static_cast<unsigned long>(card.cycleId),
      static_cast<unsigned long>(card.shotLogId), static_cast<unsigned long>(eligible ? latest.id : 0),
      static_cast<unsigned long>(retained ? kShotStreamRetainMs - age : 0),
      control.snapshotStale ? "true" : "false", base, curve.count);
  size_t used = static_cast<size_t>(cardBytes);
  bool ok = formatted && envelopeBytes > 0 && static_cast<size_t>(envelopeBytes) < sizeof(envelope);
  if (ok) {
    memmove(output + envelopeBytes, output, used + 1);
    memcpy(output, envelope, envelopeBytes);
    used += envelopeBytes;
    const int added = snprintf(output + used, NetworkWorkBuf::kJsonItem - used,
                              ",\"curve\":{%s}}", workBuf_->curveJson);
    ok = added > 0 && static_cast<size_t>(added) < NetworkWorkBuf::kJsonItem - used;
    if (ok) used += added;
  }
  if (ok) {
    // Recheck ownership immediately before delivery; telemetry never renews it.
    bool owner;
    { TaskLockGuard lock(dataMux_); owner = strcmp(session.clientId, activeWebUiClientId_) == 0; }
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t *>(output);
    frame.len = used;
    ok = owner && httpd_ws_send_frame_async(server_, session.fd, &frame) == ESP_OK;
  }
  if (!ok) { unlockWorkBuf(); httpd_sess_trigger_close(server_, session.fd); return; }
  ++session.sequence;
  session.boot = control.bootId;
  session.cycle = card.cycleId;
  session.shot = card.shotLogId;
  session.cursor = curve.count;
  session.prefixHash = prefix;
  session.markersHash = markers;
  session.fingerprint = fingerprint;
  session.resync = false;
  unlockWorkBuf();
}
