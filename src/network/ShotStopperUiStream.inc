// httpd owns framing, session contexts and serialization. The existing priority-1
// network task queues at most one dispatch; it never sends or borrows JSON scratch.
namespace {
constexpr uint32_t kUiStreamBindMs = 5000, kUiStreamRetainMs = 30000;
// Live shots keep the scale's 10 Hz; idle Home only needs edge pickup.
constexpr uint32_t kUiStreamLiveMs = 100, kUiStreamIdleMs = 250;
constexpr size_t kUiStreamRequestBytes = 160;
// Stats pages carry full curves; multi-row frames accumulate under this
// budget, one frame per dispatch tick (a continued page resumes where its
// previous frame stopped). A maximal row (~20.7 KiB curve JSON plus scalars)
// rides alone and stays far under the client's 28 KiB frame guard even with
// the ui and stats extras.
constexpr size_t kStatsStreamFrameBytes = 20480;
// WS header size: a failed write of at most this many bytes leaves the
// stream clean, because nothing of that frame reached the socket.
constexpr size_t kUiStreamHeaderMax = 14;

// Shared op validation for the view-scoped record subscriptions: an exact
// field whitelist and bounded optional integers.
bool uiStreamKnownFields(const cJSON *message, const char *const *names,
                           size_t count) {
  for (const cJSON *field = message->child; field; field = field->next) {
    bool ok = false;
    for (size_t index = 0; index < count && !ok; ++index)
      ok = field->string != nullptr &&
           strcmp(field->string, names[index]) == 0;
    if (!ok) return false;
  }
  return true;
}

bool uiStreamIntInRange(const cJSON *node, double low, double high) {
  return node == nullptr ||
         (cJSON_IsNumber(node) && node->valuedouble >= low &&
          node->valuedouble <= high && node->valuedouble == node->valueint);
}

bool uiStreamOriginMatches(const char *origin, const char *host) {
  if (!host || !host[0]) return false;
  char accepted[DEVICE_NAME_CAPACITY + 24];
  snprintf(accepted, sizeof(accepted), "http://%s", host);
  if (strcmp(origin, accepted) == 0) return true;
  snprintf(accepted, sizeof(accepted), "http://%s:80", host);
  return strcmp(origin, accepted) == 0;
}

uint32_t uiStreamCurveHash(const ShotCurveRecord &curve, uint16_t count) {
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

// Set by uiStreamSend for the senders: true when a failed call wrote
// nothing of a frame header (transient backpressure), so the frame can be
// deferred to the next dispatch; a partial payload or a dead peer leaves it
// false and the caller must close the session. httpd serializes every send
// on its own thread, so the flag needs no lock.
bool uiStreamSendCleanAbort = false;

int uiStreamSendFailed(size_t done, size_t length) {
  uiStreamSendCleanAbort =
      done == 0 && length <= kUiStreamHeaderMax &&
      errno != EPIPE && errno != EBADF && errno != ECONNRESET &&
      errno != ENOTCONN && errno != ENOTSOCK;
  return -1;
}

// IDF's WS sender calls send_fn once per header/payload and accepts short writes.
// Complete each write or fail within 100 ms, including slow-client backpressure.
int uiStreamSend(httpd_handle_t, int fd, const char *data, size_t length, int flags) {
  const uint32_t started = millis();
  size_t done = 0;
  while (done < length) {
    const uint32_t age = millis() - started;
    if (age >= 100) {
      errno = EAGAIN;
      return uiStreamSendFailed(done, length);
    }
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(fd, &writable);
    timeval wait = {0, static_cast<suseconds_t>((100 - age) * 1000)};
    const int ready = select(fd + 1, nullptr, &writable, nullptr, &wait);
    if (ready < 0 && (errno == EBADF || errno == EINVAL))
      return uiStreamSendFailed(done, length);
    if (ready <= 0) {
      // The write budget ran out with the socket still congested.
      errno = EAGAIN;
      return uiStreamSendFailed(done, length);
    }
    const int sent = send(fd, data + done, length - done, flags | MSG_DONTWAIT);
    if (sent > 0) done += static_cast<size_t>(sent);
    else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
      return uiStreamSendFailed(done, length);
  }
  return static_cast<int>(done);
}
}  // namespace

#include "ShotStopperHomeStream.inc"

esp_err_t ShotStopperNetwork::uiStreamHandshake(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  char origin[DEVICE_NAME_CAPACITY + 24] = {}, local[DEVICE_NAME_CAPACITY + 8] = {};
  const size_t length = httpd_req_get_hdr_value_len(request, "Origin");
  if (length == 0 || length >= sizeof(origin) ||
      httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof(origin)) != ESP_OK)
    return ESP_FAIL;
  const NetworkStatusSnapshot network = self.snapshot();
  snprintf(local, sizeof(local), "%s.local", network.mdnsHost);
  if (!uiStreamOriginMatches(origin, network.staIp) &&
      !uiStreamOriginMatches(origin, network.apIp) &&
      !uiStreamOriginMatches(origin, local)) return ESP_FAIL;
  TaskLockGuard lock(self.dataMux_);
  for (auto &session : self.uiStreams_) {
    if (session.fd != -1) continue;
    session = UiStreamSession{};
    session.fd = httpd_req_to_sockfd(request);
    session.openedAtMs = millis();
    request->sess_ctx = &session;
    request->free_ctx = uiStreamFree;
    // Bound incomplete incoming frames independently of ordinary HTTP/OTA.
    const timeval timeout = {0, 100000};
    setsockopt(session.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return ESP_OK;
  }
  return ESP_FAIL;
}

void ShotStopperNetwork::uiStreamFree(void *context) {
  if (!instance_ || !context) return;
  TaskLockGuard lock(instance_->dataMux_);
  *static_cast<UiStreamSession *>(context) = UiStreamSession{};
}

esp_err_t ShotStopperNetwork::uiStreamHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  auto *session = static_cast<UiStreamSession *>(request->sess_ctx);
  if (!session) return ESP_FAIL;
  httpd_ws_frame_t frame = {};
  char body[kUiStreamRequestBytes + 1] = {};
  if (httpd_ws_recv_frame(request, &frame, 0) != ESP_OK ||
      frame.type != HTTPD_WS_TYPE_TEXT || !frame.final ||
      frame.len == 0 || frame.len > kUiStreamRequestBytes) return ESP_FAIL;
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
      session->diagResync = session->diagnostic;
      session->historyResync = true;
      session->statsResync = true;
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
    } else if (cJSON_IsString(op) && session->bound &&
               strcmp(op->valuestring, "log") == 0 &&
               strcmp(session->clientId, self.activeWebUiClientId_) == 0) {
      // View-scoped serial-log toggle: the cursor comes from the client's
      // rendered tail; unsubscribe carries no window fields.
      const cJSON *on = cJSON_GetObjectItemCaseSensitive(message, "on");
      const cJSON *after = cJSON_GetObjectItemCaseSensitive(message, "after");
      bool shapeOk = cJSON_IsBool(on);
      for (const cJSON *field = message->child; shapeOk && field;
           field = field->next) {
        const char *name = field->string;
        shapeOk = name && (!strcmp(name, "op") || !strcmp(name, "on") ||
                           !strcmp(name, "after"));
      }
      const bool numbersOk =
          !after || (cJSON_IsNumber(after) && after->valuedouble >= 0 &&
                     after->valuedouble <= UINT32_MAX &&
                     after->valuedouble == after->valueint);
      if (shapeOk && numbersOk) {
        if (!cJSON_IsTrue(on)) {
          accepted = !after;
          if (accepted) session->logOn = false;
        } else {
          session->logOn = true;
          session->logAfter = after ? static_cast<uint32_t>(after->valueint) : 0;
          session->logPush = true;
          accepted = true;
        }
        if (accepted)
          self.uiStreamUrgent_.store(true, std::memory_order_release);
      }
    } else if (cJSON_IsString(op) && session->bound &&
               strcmp(op->valuestring, "history") == 0 &&
               strcmp(session->clientId, self.activeWebUiClientId_) == 0) {
      // View-scoped History page subscription: the standing window tracks the
      // page; a one-shot fetch serves sentinel appends without disturbing it.
      static const char *const kFields[] = {"op", "on", "offset",
                                            "limit", "dir", "fetch"};
      const cJSON *on = cJSON_GetObjectItemCaseSensitive(message, "on");
      const cJSON *offset = cJSON_GetObjectItemCaseSensitive(message, "offset");
      const cJSON *limit = cJSON_GetObjectItemCaseSensitive(message, "limit");
      const cJSON *dir = cJSON_GetObjectItemCaseSensitive(message, "dir");
      const cJSON *fetch = cJSON_GetObjectItemCaseSensitive(message, "fetch");
      const bool shapeOk =
          cJSON_IsBool(on) && uiStreamKnownFields(message, kFields, 6);
      const bool numbersOk =
          uiStreamIntInRange(offset, 0, HISTORY_CAPACITY) &&
          uiStreamIntInRange(limit, 1, HISTORY_PAGE_MAX) &&
          (!fetch || cJSON_IsBool(fetch)) &&
          (!dir || (cJSON_IsString(dir) &&
                    (!strcmp(dir->valuestring, "asc") ||
                     !strcmp(dir->valuestring, "desc"))));
      if (shapeOk && numbersOk) {
        if (!cJSON_IsTrue(on)) {
          // Unsubscribe carries no window fields.
          accepted = !offset && !limit && !dir && !fetch;
          if (accepted) {
            session->historyOn = false;
            session->historyFetch = false;
          }
        } else if (fetch && cJSON_IsTrue(fetch)) {
          accepted = offset != nullptr;
          if (accepted) {
            session->historyFetchOffset =
                static_cast<size_t>(offset->valueint);
            session->historyFetch = true;
          }
        } else {
          session->historyOn = true;
          session->historyResync = true;
          if (offset)
            session->historyOffset = static_cast<size_t>(offset->valueint);
          if (limit)
            session->historyLimit = static_cast<size_t>(limit->valueint);
          if (dir)
            session->historyDir = !strcmp(dir->valuestring, "asc")
                                       ? ShotLogSortDir::Asc
                                       : ShotLogSortDir::Desc;
          accepted = true;
        }
        if (accepted)
          self.uiStreamUrgent_.store(true, std::memory_order_release);
      }
    } else if (cJSON_IsString(op) && session->bound &&
               strcmp(op->valuestring, "stats") == 0 &&
               strcmp(session->clientId, self.activeWebUiClientId_) == 0) {
      // View-scoped Stats page subscription on the same contract; one-shot
      // fetches also serve the CSV export window (limit 100, date/desc).
      static const char *const kFields[] = {"op",    "on",    "offset",
                                            "limit", "sort",  "dir",
                                            "fetch"};
      const cJSON *on = cJSON_GetObjectItemCaseSensitive(message, "on");
      const cJSON *offset = cJSON_GetObjectItemCaseSensitive(message, "offset");
      const cJSON *limit = cJSON_GetObjectItemCaseSensitive(message, "limit");
      const cJSON *sort = cJSON_GetObjectItemCaseSensitive(message, "sort");
      const cJSON *dir = cJSON_GetObjectItemCaseSensitive(message, "dir");
      const cJSON *fetch = cJSON_GetObjectItemCaseSensitive(message, "fetch");
      const bool shapeOk =
          cJSON_IsBool(on) && uiStreamKnownFields(message, kFields, 7);
      const bool numbersOk =
          uiStreamIntInRange(offset, 0, SHOT_LOG_CAPACITY) &&
          uiStreamIntInRange(limit, 1, SHOT_LOG_CAPACITY) &&
          (!fetch || cJSON_IsBool(fetch)) &&
          (!sort || (cJSON_IsString(sort) &&
                     (!strcmp(sort->valuestring, "date") ||
                      !strcmp(sort->valuestring, "rating")))) &&
          (!dir || (cJSON_IsString(dir) &&
                    (!strcmp(dir->valuestring, "asc") ||
                     !strcmp(dir->valuestring, "desc"))));
      if (shapeOk && numbersOk) {
        if (!cJSON_IsTrue(on)) {
          accepted = !offset && !limit && !sort && !dir && !fetch;
          if (accepted) {
            session->statsOn = false;
            session->statsFetch = false;
            session->statsPaging = false;
          }
        } else if (fetch && cJSON_IsTrue(fetch)) {
          accepted = offset != nullptr;
          if (accepted) {
            session->statsFetchOffset =
                static_cast<size_t>(offset->valueint);
            session->statsFetchLimit = limit
                ? static_cast<size_t>(limit->valueint)
                : session->statsLimit;
            session->statsFetchSort = sort
                ? shotLogSortFromName(sort->valuestring)
                : session->statsSort;
            session->statsFetchDir = dir
                ? shotLogSortDirFromName(dir->valuestring)
                : session->statsDir;
            session->statsFetch = true;
          }
        } else {
          session->statsOn = true;
          session->statsResync = true;
          if (offset)
            session->statsOffset = static_cast<size_t>(offset->valueint);
          if (limit)
            session->statsLimit = static_cast<size_t>(limit->valueint);
          if (sort)
            session->statsSort = shotLogSortFromName(sort->valuestring);
          if (dir)
            session->statsDir = shotLogSortDirFromName(dir->valuestring);
          accepted = true;
        }
        if (accepted)
          self.uiStreamUrgent_.store(true, std::memory_order_release);
      }
    } else if (cJSON_IsString(op) && !session->bound &&
               strcmp(op->valuestring, "bind") == 0 && cJSON_IsString(client) &&
               self.activeWebUiClientId_[0] &&
               strcmp(client->valuestring, self.activeWebUiClientId_) == 0 &&
               millis() - session->openedAtMs < kUiStreamBindMs) {
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
      for (auto &other : self.uiStreams_) {
        if (&other != session && other.bound) {
          superseded = other.fd;
          other.bound = false;
          other.openedAtMs = millis() - kUiStreamBindMs;
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
  httpd_sess_set_send_override(request->handle, session->fd, uiStreamSend);
  self.requestPendingNetworkConfirm();
  return ESP_OK;
}

void ShotStopperNetwork::serviceUiStream(uint32_t now) {
  if (!server_) return;
  const uint32_t cadence = controlCriticalRfActive_.load(std::memory_order_acquire)
                                ? kUiStreamLiveMs : kUiStreamIdleMs;
  if (!uiStreamUrgent_.load(std::memory_order_acquire) &&
      now - uiStreamDispatchAtMs_ < cadence) return;
  bool occupied = false;
  {
    TaskLockGuard lock(dataMux_);
    for (const auto &session : uiStreams_) occupied |= session.fd >= 0;
  }
  if (!occupied || uiStreamWorkPending_.exchange(true, std::memory_order_acq_rel)) return;
  uiStreamUrgent_.exchange(false, std::memory_order_acq_rel);
  uiStreamDispatchAtMs_ = now;
  if (httpd_queue_work(server_, uiStreamDispatch, this) != ESP_OK) {
    uiStreamUrgent_.store(true, std::memory_order_release);
    uiStreamWorkPending_.store(false, std::memory_order_release);
  }
}

void ShotStopperNetwork::uiStreamDispatch(void *context) {
  auto &self = *static_cast<ShotStopperNetwork *>(context);
  for (auto &session : self.uiStreams_) {
    if (session.fd < 0) continue;
    bool allowed;
    {
      TaskLockGuard lock(self.dataMux_);
      allowed = session.bound
          ? strcmp(session.clientId, self.activeWebUiClientId_) == 0
          : millis() - session.openedAtMs < kUiStreamBindMs;
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
      self.sendUiStream(session);
    }
  }
  self.uiStreamWorkPending_.store(false, std::memory_order_release);
}

void ShotStopperNetwork::sendUiStream(UiStreamSession &session) {
  // httpd serialization is exclusive. Try rather than wait behind Network's
  // occasional settings work; the next dispatch recovers a complete view.
  if (xSemaphoreTake(statusResponseMux_.get(), 0) != pdTRUE) {
    uiStreamUrgent_.store(true, std::memory_order_release);
    return;
  }
  callbacks_.refreshControlStatus();
  ControlStatusSnapshot &control = workBuf_->control;
  loadControlStatus(control);
  // The RF notification can precede the committed presentation snapshot.
  if ((control.activeCycle || control.relayClosed) !=
      controlCriticalRfActive_.load(std::memory_order_acquire))
    uiStreamUrgent_.store(true, std::memory_order_release);
  ShotLogRecord latest = {};
  const bool eligible = callbacks_.copyHomeShot &&
      callbacks_.copyHomeShot(latest, workBuf_->homeCurve);
  // Home first, then the shot card: the client's setup window waits for both
  // and must not queue behind record pages, which can pace across dispatches.
  if (!sendHomeStream(session, control, eligible ? &latest : nullptr)) {
    unlockWorkBuf();
    httpd_sess_trigger_close(server_, session.fd);
    return;
  }
  if (!sendShotCard(session, control, latest, eligible)) {
    unlockWorkBuf();
    httpd_sess_trigger_close(server_, session.fd);
    return;
  }
  if (session.diagnostic && !sendDiagnosticStream(session, control)) {
    unlockWorkBuf();
    httpd_sess_trigger_close(server_, session.fd);
    return;
  }
  if (session.historyOn && !sendHistoryStream(session, control)) {
    unlockWorkBuf();
    httpd_sess_trigger_close(server_, session.fd);
    return;
  }
  if (session.statsOn && !sendStatsStream(session, control)) {
    unlockWorkBuf();
    httpd_sess_trigger_close(server_, session.fd);
    return;
  }
  if (session.logOn && !sendLogStream(session, control)) {
    unlockWorkBuf();
    httpd_sess_trigger_close(server_, session.fd);
    return;
  }
  unlockWorkBuf();
}

bool ShotStopperNetwork::sendShotCard(UiStreamSession &session,
                                      const ControlStatusSnapshot &control,
                                      const ShotLogRecord &latest, bool eligible) {
  ShotCurveRecord &curve = workBuf_->serializedCurve;
  const PersistedLastShot &cycle = control.homeCycle;
  const bool live = control.activeCycle || control.homePending;
  if (live) {
    session.observedCycle = cycle.cycleId;
    session.observedBoot = control.bootId;
  }
  const uint32_t age = millis() - control.homeResolvedAtMs;
  const bool retained = !live && cycle.valid && cycle.shotLogId == 0 &&
      session.observedBoot == control.bootId && session.observedCycle == cycle.cycleId &&
      age < kUiStreamRetainMs;
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
  if (cardBytes <= 0 || static_cast<size_t>(cardBytes) >= NetworkWorkBuf::kJsonItem)
    return true;
  const uint32_t markers = crc32Update(0xFFFFFFFFU,
      reinterpret_cast<const uint8_t *>(&curve.atmClearedMs),
      offsetof(ShotCurveRecord, weightCg) - offsetof(ShotCurveRecord, atmClearedMs));
  const uint32_t prefix = uiStreamCurveHash(curve, curve.count);
  uint32_t fingerprint = crc32Update(prefix, reinterpret_cast<const uint8_t *>(output), cardBytes);
  fingerprint = crc32Update(fingerprint, reinterpret_cast<const uint8_t *>(&markers), sizeof(markers));
  fingerprint ^= curve.truncated ? 1U : 0U;
  fingerprint ^= control.snapshotStale ? 2U : 0U;
  const bool replace = session.resync || session.boot != control.bootId ||
      session.cycle != card.cycleId || session.shot != card.shotLogId ||
      session.cursor > curve.count || session.markersHash != markers ||
      session.prefixHash != uiStreamCurveHash(curve, session.cursor);
  if (!replace && session.fingerprint == fingerprint)
    return true;
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
      static_cast<unsigned long>(retained ? kUiStreamRetainMs - age : 0),
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
  bool owner = false;
  if (ok) {
    // Recheck ownership immediately before delivery; telemetry never renews it.
    { TaskLockGuard lock(dataMux_); owner = strcmp(session.clientId, activeWebUiClientId_) == 0; }
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t *>(output);
    frame.len = used;
    ok = owner && httpd_ws_send_frame_async(server_, session.fd, &frame) == ESP_OK;
  }
  if (!ok) {
    // A clean header abort defers: every bookkeeping flag below is only
    // cleared on delivery, so the next dispatch resends the same card.
    return owner && uiStreamSendCleanAbort;
  }
  ++session.sequence;
  session.boot = control.bootId;
  session.cycle = card.cycleId;
  session.shot = card.shotLogId;
  session.cursor = curve.count;
  session.prefixHash = prefix;
  session.markersHash = markers;
  session.fingerprint = fingerprint;
  session.resync = false;
  return true;
}
