#include "ShotStopperWebhook.h"
#include "ShotStopperPsram.h"

#if defined(SHOT_STOPPER_WEBHOOK_TEST_PLATFORM) || \
    (!defined(SHOT_STOPPER_HOST_TEST) && \
     !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST))

#if !defined(SHOT_STOPPER_WEBHOOK_TEST_PLATFORM)
#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_client.h>
#include <esp_mac.h>
#endif

namespace shotstopper {
namespace {

constexpr size_t kWebhookQueueDepth = 4;
constexpr size_t kWebhookPayloadCapacity = 1024;
constexpr int kWebhookTimeoutMs = 1800;
constexpr uint32_t kWebhookStopTimeoutMs = 2500;

const char *eventName(WebhookEventType type) {
  switch (type) {
    case WebhookEventType::BREWING: return "brew_state";
    case WebhookEventType::IDLE: return "brew_state";
    case WebhookEventType::FIRST_DROP: return "first_drop";
    case WebhookEventType::END: return "end";
    case WebhookEventType::TEST: return "test";
  }
  return "unknown";
}

void deviceId(char *output, size_t capacity) {
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    snprintf(output, capacity, "unknown");
    return;
  }
  snprintf(output, capacity, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

}  // namespace

bool WebhookDispatcher::begin(const WebhookConfig &config) {
  if (lifecycleMutex_ != nullptr || workerStopped_ != nullptr) return false;
  mux_.lock();
  config_ = config;
  configGeneration_ = 1;
  deferDuringShot_.store(config.deferDuringShot, std::memory_order_release);
  mux_.unlock();
  lifecycleMutex_ = xSemaphoreCreateMutex();
  if (lifecycleMutex_ == nullptr) {
    mux_.lock();
    ++status_.workerStartFailures;
    mux_.unlock();
    return false;
  }
  workerStopped_ = xSemaphoreCreateBinary();
  if (workerStopped_ == nullptr) {
    vSemaphoreDelete(lifecycleMutex_);
    lifecycleMutex_ = nullptr;
    mux_.lock();
    ++status_.workerStartFailures;
    mux_.unlock();
    return false;
  }
  if (!config.enabled || startWorker()) return true;
  // startWorker() already released its queue/payload attempt. Complete the
  // failed transaction by releasing the passive lifecycle objects as well.
  (void)stop();
  return false;
}

bool WebhookDispatcher::stop() {
  if (lifecycleMutex_ == nullptr) {
    if (workerStopped_ != nullptr) {
      vSemaphoreDelete(workerStopped_);
      workerStopped_ = nullptr;
    }
    return true;
  }

  TaskHandle_t worker = nullptr;
  if (xSemaphoreTake(lifecycleMutex_, pdMS_TO_TICKS(kWebhookStopTimeoutMs)) !=
      pdTRUE) {
    return false;
  }
  worker = task_;
  if (worker != nullptr) {
    workerState_ = WorkerState::STOPPING;
    stopAfterDrain_ = false;
  }
  xSemaphoreGive(lifecycleMutex_);

  if (worker != nullptr) {
    xTaskNotifyGive(worker);
    if (workerStopped_ == nullptr ||
        xSemaphoreTake(workerStopped_, pdMS_TO_TICKS(kWebhookStopTimeoutMs)) !=
            pdTRUE) {
      return false;
    }
  }

  vSemaphoreDelete(lifecycleMutex_);
  lifecycleMutex_ = nullptr;
  vSemaphoreDelete(workerStopped_);
  workerStopped_ = nullptr;
  abortRequested_.store(false, std::memory_order_release);
  cancelActive_.store(false, std::memory_order_release);
  activeCloseError_.store(0, std::memory_order_release);
  return true;
}

bool WebhookDispatcher::startWorker() {
  if (lifecycleMutex_ == nullptr ||
      xSemaphoreTake(lifecycleMutex_, portMAX_DELAY) != pdTRUE) return false;
  if (workerState_ == WorkerState::READY) {
    xSemaphoreGive(lifecycleMutex_);
    return true;
  }
  if (workerState_ == WorkerState::STARTING ||
      workerState_ == WorkerState::STOPPING) {
    xSemaphoreGive(lifecycleMutex_);
    return false;
  }
  workerState_ = WorkerState::STARTING;
  stopAfterDrain_ = false;
  // A worker disabled by setConfig() may have left an unconsumed completion.
  // Never let that stale acknowledgement satisfy a later stop()/join.
  if (workerStopped_ != nullptr) (void)xSemaphoreTake(workerStopped_, 0);

  // FreeRTOS copies queue items under its spinlock: keep these 368 bytes
  // internal. The larger HTTP payload remains external and fails closed.
  const size_t queueStorageBytes =
      kWebhookQueueDepth * sizeof(QueuedWebhook);
  uint8_t *queueStorage =
      static_cast<uint8_t *>(allocInternal(queueStorageBytes, AllocationOwner::WEBHOOK));
  char *payload =
      static_cast<char *>(allocExternal(kWebhookPayloadCapacity, AllocationOwner::WEBHOOK));
  QueueHandle_t queue = nullptr;
  if (queueStorage != nullptr) {
    queue = xQueueCreateStatic(kWebhookQueueDepth, sizeof(QueuedWebhook),
                               queueStorage, &queueControl_);
  }
  if (queue == nullptr || payload == nullptr) {
    if (queue != nullptr) vQueueDelete(queue);
    heapCapsFree(queueStorage);
    heapCapsFree(payload);
    mux_.lock();
    workerState_ = WorkerState::STOPPED;
    status_.workerReady = false;
    ++status_.workerStartFailures;
    mux_.unlock();
    xSemaphoreGive(lifecycleMutex_);
    return false;
  }
  mux_.lock();
  queue_ = queue;
  queueStorage_ = queueStorage;
  payload_ = payload;
  mux_.unlock();
  if (xTaskCreatePinnedToCore(taskEntry, "webhook", 4096, this,
                             tskIDLE_PRIORITY, &task_, 0) != pdPASS) {
    vQueueDelete(queue);
    heapCapsFree(queueStorage);
    heapCapsFree(payload);
    mux_.lock();
    queue_ = nullptr;
    queueStorage_ = nullptr;
    payload_ = nullptr;
    task_ = nullptr;
    workerState_ = WorkerState::STOPPED;
    status_.workerReady = false;
    ++status_.workerStartFailures;
    mux_.unlock();
    xSemaphoreGive(lifecycleMutex_);
    return false;
  }
  mux_.lock();
  workerState_ = WorkerState::READY;
  status_.workerReady = true;
  ++status_.workerStarts;
  mux_.unlock();
  xSemaphoreGive(lifecycleMutex_);
  return true;
}

void WebhookDispatcher::releaseWorkerFromTask() {
  cleanupHttpClient();
  QueueHandle_t queue = nullptr;
  uint8_t *queueStorage = nullptr;
  char *payload = nullptr;
  if (lifecycleMutex_ != nullptr &&
      xSemaphoreTake(lifecycleMutex_, portMAX_DELAY) == pdTRUE) {
    queue = queue_;
    queueStorage = queueStorage_;
    payload = payload_;
    queue_ = nullptr;
    queueStorage_ = nullptr;
    payload_ = nullptr;
    task_ = nullptr;
    workerState_ = WorkerState::STOPPED;
    stopAfterDrain_ = false;
    mux_.lock();
    status_.workerReady = false;
    status_.sending = false;
    ++status_.workerStops;
    mux_.unlock();
    xSemaphoreGive(lifecycleMutex_);
  }
  if (queue != nullptr) vQueueDelete(queue);
  heapCapsFree(queueStorage);
  heapCapsFree(payload);
  if (workerStopped_ != nullptr) xSemaphoreGive(workerStopped_);
}

esp_http_client_handle_t WebhookDispatcher::ensureHttpClient(const char *url) {
  if (!validWebhookUrl(url)) return nullptr;
  if (httpClient_ &&
      !webhookClientMustRecreate(httpClientUrl_, url)) {
    mux_.lock();
    ++status_.clientReuses;
    mux_.unlock();
    return httpClient_.get();
  }

  cleanupHttpClient();
  esp_http_client_config_t config = {};
  config.url = url;
  config.timeout_ms = kWebhookTimeoutMs;
  config.disable_auto_redirect = true;
  config.user_data = this;
  config.event_handler = httpEventHandler;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) return nullptr;
  httpClient_.reset(client);
  const size_t length = strnlen(url, sizeof(httpClientUrl_) - 1U);
  memcpy(httpClientUrl_, url, length);
  httpClientUrl_[length] = '\0';
  mux_.lock();
  ++status_.clientCreates;
  mux_.unlock();
  return client;
}

void WebhookDispatcher::cleanupHttpClient() {
  if (!httpClient_) {
    httpClientUrl_[0] = '\0';
    return;
  }
  esp_http_client_handle_t client = httpClient_.release();
  httpClientUrl_[0] = '\0';
  const esp_err_t cleanupError = esp_http_client_cleanup(client);
  mux_.lock();
  ++status_.clientCleanups;
  if (cleanupError != ESP_OK) status_.lastError = cleanupError;
  mux_.unlock();
}

void WebhookDispatcher::setConfig(const WebhookConfig &config) {
  // A previous begin() can fail transactionally (for example, no PSRAM).
  // Permit a later configuration update to retry the full initialization.
  if (lifecycleMutex_ == nullptr) {
    (void)begin(config);
    return;
  }
  bool changed = false;
  mux_.lock();
  changed = memcmp(&config_, &config, sizeof(config)) != 0;
  if (changed) {
    config_ = config;
    deferDuringShot_.store(config.deferDuringShot, std::memory_order_release);
    ++configGeneration_;
    if (configGeneration_ == 0) configGeneration_ = 1;
  }
  mux_.unlock();
  // Once created, keep the worker and its PSRAM buffers idle across ordinary
  // enable/disable changes. This avoids task/queue/stack churn under repeated
  // configuration updates. stop() remains the sole lifecycle teardown and
  // still performs stop/ack/join before releasing resources.
  if (config.enabled) (void)startWorker();
}

WebhookConfig WebhookDispatcher::config() const {
  WebhookConfig copy;
  mux_.lock();
  copy = config_;
  mux_.unlock();
  return copy;
}

WebhookStatus WebhookDispatcher::status() const {
  WebhookStatus copy;
  mux_.lock();
  copy = status_;
  mux_.unlock();
  return copy;
}

void WebhookDispatcher::setControlCritical(bool active) {
  controlCritical_.store(active, std::memory_order_release);
  if (active) {
    cancelActive_.store(true, std::memory_order_release);
    abortRequested_.store(true, std::memory_order_release);
  }
}

void WebhookDispatcher::setScaleConnecting(bool active) {
  scaleConnecting_.store(active, std::memory_order_release);
  if (active) {
    cancelActive_.store(true, std::memory_order_release);
    abortRequested_.store(true, std::memory_order_release);
  }
}

bool WebhookDispatcher::dispatchAllowed() const {
  return (!deferDuringShot_.load(std::memory_order_acquire) ||
          !controlCritical_.load(std::memory_order_acquire)) &&
         !scaleConnecting_.load(std::memory_order_acquire);
}

void WebhookDispatcher::serviceAbort() {
  if (!abortRequested_.exchange(false, std::memory_order_acq_rel)) return;

  esp_http_client_handle_t client = nullptr;
  mux_.lock();
  if (activeClient_ != nullptr && !cancelInProgress_) {
    cancelInProgress_ = true;
    ++activeClientUsers_;
    client = static_cast<esp_http_client_handle_t>(activeClient_);
  }
  mux_.unlock();
  if (client == nullptr) return;

  // This is the ESP-IDF API intended to interrupt a blocking perform from a
  // different task. Its reconnect is immediately closed by the event handler
  // below while the RF gate remains active.
  const esp_err_t result = esp_http_client_cancel_request(client);
  mux_.lock();
  cancelInProgress_ = false;
  if (activeClientUsers_ > 0) --activeClientUsers_;
  const bool stillActive = activeClient_ == client;
  mux_.unlock();
  if (stillActive && result != ESP_OK &&
      cancelActive_.load(std::memory_order_acquire)) {
    // CONNECTING/DNS is not cancellable until the client reaches CONNECTED.
    // Retry from the next 20 Hz network-manager pass.
    abortRequested_.store(true, std::memory_order_release);
  }
}

bool WebhookDispatcher::enqueue(const WebhookEvent &event) {
  if (event.type == WebhookEventType::TEST) (void)startWorker();
  WebhookConfig live;
  QueuedWebhook queued;
  queued.event = event;
  QueueHandle_t queue = nullptr;
  WorkerState workerState = WorkerState::STOPPED;
  if (lifecycleMutex_ == nullptr ||
      xSemaphoreTake(lifecycleMutex_, 0) != pdTRUE) {
    mux_.lock();
    ++status_.dropped;
    mux_.unlock();
    return false;
  }
  mux_.lock();
  live = config_;
  queued.configGeneration = configGeneration_;
  queue = queue_;
  workerState = workerState_;
  mux_.unlock();
  bool selected = event.type == WebhookEventType::TEST;
  if (event.type == WebhookEventType::BREWING ||
      event.type == WebhookEventType::IDLE) selected = live.brewState;
  if (event.type == WebhookEventType::FIRST_DROP) selected = live.firstDrop;
  if (event.type == WebhookEventType::END) selected = live.end;
  if (workerState != WorkerState::READY || queue == nullptr ||
      !validWebhookUrl(live.url) ||
      (event.type != WebhookEventType::TEST && (!live.enabled || !selected)) ||
      xQueueSend(queue, &queued, 0) != pdTRUE) {
    xSemaphoreGive(lifecycleMutex_);
    mux_.lock();
    ++status_.dropped;
    mux_.unlock();
    return false;
  }
  if (event.type == WebhookEventType::TEST && !live.enabled) {
    stopAfterDrain_ = true;
  }
  xSemaphoreGive(lifecycleMutex_);
  return true;
}

void WebhookDispatcher::taskEntry(void *parameter) {
  static_cast<WebhookDispatcher *>(parameter)->task();
}

esp_err_t WebhookDispatcher::httpEventHandler(esp_http_client_event_t *event) {
  if (event == nullptr || event->user_data == nullptr ||
      event->event_id == HTTP_EVENT_ERROR ||
      event->event_id == HTTP_EVENT_DISCONNECTED) {
    return ESP_OK;
  }
  auto *dispatcher = static_cast<WebhookDispatcher *>(event->user_data);
  if (!dispatcher->dispatchAllowed() ||
      dispatcher->cancelActive_.load(std::memory_order_acquire)) {
    dispatcher->abortRequested_.store(true, std::memory_order_release);
    // ESP-IDF ignores event-handler return values in perform(). Closing the
    // transport here makes connected/header/data events actually abort.
    // DISCONNECTED is excluded above to avoid recursive close dispatch.
    if (event->client != nullptr) {
      const esp_err_t closeError = esp_http_client_close(event->client);
      // INVALID_STATE is expected if cancellation won the race and already
      // closed the transport. Any other failure remains visible in status.
      if (closeError != ESP_OK && closeError != ESP_ERR_INVALID_STATE) {
        int32_t expected = 0;
        (void)dispatcher->activeCloseError_.compare_exchange_strong(
            expected, static_cast<int32_t>(closeError),
            std::memory_order_acq_rel);
      }
    }
  }
  return ESP_OK;
}

void WebhookDispatcher::task() {
  QueuedWebhook queued;
  bool haveQueued = false;
  for (;;) {
    QueueHandle_t queue = nullptr;
    WorkerState state = WorkerState::STOPPED;
    if (lifecycleMutex_ != nullptr &&
        xSemaphoreTake(lifecycleMutex_, portMAX_DELAY) == pdTRUE) {
      queue = queue_;
      state = workerState_;
      xSemaphoreGive(lifecycleMutex_);
    }
    if (state == WorkerState::STOPPING || queue == nullptr) break;
    // Leave events queued while a shot/rinse or scale connection attempt owns
    // radio time. Queue operations and HTTP remain entirely off control/BLE.
    bool waitedForQueue = false;
    if (!haveQueued && dispatchAllowed()) {
      waitedForQueue = true;
      haveQueued =
          xQueueReceive(queue, &queued, pdMS_TO_TICKS(50)) == pdTRUE;
    }
    // The gate may change while xQueueReceive is blocked. Keep the dequeued
    // item locally and recheck so no request starts after the critical edge.
    if (haveQueued && dispatchAllowed()) {
      (void)send(queued);
      haveQueued = false;
    } else if (haveQueued || !waitedForQueue) {
      vTaskDelay(pdMS_TO_TICKS(25));
    }
    if (lifecycleMutex_ != nullptr &&
        xSemaphoreTake(lifecycleMutex_, portMAX_DELAY) == pdTRUE) {
      if (workerState_ == WorkerState::READY && stopAfterDrain_ && !haveQueued &&
          uxQueueMessagesWaiting(queue_) == 0) {
        workerState_ = WorkerState::STOPPING;
      }
      state = workerState_;
      xSemaphoreGive(lifecycleMutex_);
    }
    if (state == WorkerState::STOPPING) break;
  }
  releaseWorkerFromTask();
  vTaskDelete(nullptr);
}

bool WebhookDispatcher::buildPayload(const WebhookEvent &event, char *output,
                                     size_t capacity) {
  char id[18] = {};
  deviceId(id, sizeof(id));
  int written = snprintf(
      output, capacity,
      "{\"schemaVersion\":1,\"event\":\"%s\",\"deviceId\":\"%s\","
      "\"cycleId\":%lu,\"uptimeMs\":%lu,\"timestamp\":%lu,"
      "\"sentAtUptimeMs\":%lu",
      eventName(event.type), id, static_cast<unsigned long>(event.cycleId),
      static_cast<unsigned long>(event.uptimeMs),
      static_cast<unsigned long>(event.unixSec),
      static_cast<unsigned long>(millis()));
  if (written <= 0 || static_cast<size_t>(written) >= capacity) return false;
  size_t used = static_cast<size_t>(written);
  auto append = [&](const char *format, auto... args) {
    if (used >= capacity) return false;
    // The lambda always receives a string literal at every call site; the
    // non-literal parameter is what makes -Wformat-security fire. Suppress it
    // here only: NOLINT is not honored for compiler diagnostics, and the
    // GCC-style pragma is the form both GCC and clang accept.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
    const int count =
        snprintf(output + used, capacity - used, format, args...);
#pragma GCC diagnostic pop
    if (count <= 0 || static_cast<size_t>(count) >= capacity - used) return false;
    used += static_cast<size_t>(count);
    return true;
  };
  switch (event.type) {
    case WebhookEventType::BREWING:
    case WebhookEventType::IDLE:
      if (!append(",\"state\":\"%s\",\"durationMs\":%lu,"
                  "\"targetWeightG\":%.2f,\"presetId\":%u,"
                  "\"stopDetail\":\"%s\"",
                  event.type == WebhookEventType::BREWING ? "brewing" : "idle",
                  static_cast<unsigned long>(event.durationMs),
                  event.targetWeightG, static_cast<unsigned>(event.presetId),
                  event.stopDetail)) return false;
      break;
    case WebhookEventType::FIRST_DROP:
      if (!append(",\"firstDropMs\":%lu,\"weightG\":%.2f,"
                  "\"targetWeightG\":%.2f,\"presetId\":%u",
                  static_cast<unsigned long>(event.firstDropMs), event.weightG,
                  event.targetWeightG, static_cast<unsigned>(event.presetId))) {
        return false;
      }
      break;
    case WebhookEventType::END:
      if (!append(",\"durationMs\":%lu,\"targetWeightG\":%.2f,"
                  "\"presetId\":%u,\"shotType\":\"%s\","
                  "\"stopDetail\":\"%s\"",
                  static_cast<unsigned long>(event.durationMs),
                  event.targetWeightG, static_cast<unsigned>(event.presetId),
                  event.shotType, event.stopDetail)) return false;
      if (event.firstDropValid &&
          !append(",\"firstDropMs\":%lu",
                  static_cast<unsigned long>(event.firstDropMs))) return false;
      if (event.weightValid && !append(",\"weightG\":%.2f", event.weightG))
        return false;
      if (event.averageFlowValid &&
          !append(",\"averageFlowGps\":%.2f", event.averageFlowGps))
        return false;
      break;
    case WebhookEventType::TEST:
      break;
  }
  return append("}");
}

bool WebhookDispatcher::send(const QueuedWebhook &queued) {
  WebhookConfig live;
  uint32_t generation = 0;
  mux_.lock();
  live = config_;
  generation = configGeneration_;
  mux_.unlock();
  if (queued.configGeneration != generation) {
    mux_.lock();
    ++status_.dropped;
    ++status_.staleConfigDropped;
    mux_.unlock();
    return false;
  }
  const HeapCapSnapshot heapBefore = sampleHeapCaps();
  const WebhookEvent &event = queued.event;
  mux_.lock();
  status_.sending = true;
  status_.lastAttemptAtMs = millis();
  status_.lastHttpStatus = 0;
  status_.lastError = 0;
  mux_.unlock();

  bool ok = false;
  int statusCode = 0;
  esp_err_t error = ESP_FAIL;
  if (WiFi.status() == WL_CONNECTED && validWebhookUrl(live.url) &&
      buildPayload(event, payload_, kWebhookPayloadCapacity)) {
    esp_http_client_handle_t client = ensureHttpClient(live.url);
    if (client != nullptr) {
      error = static_cast<esp_err_t>(configureWebhookHttpRequest(
          [&]() {
            return static_cast<int32_t>(
                esp_http_client_set_method(client, HTTP_METHOD_POST));
          },
          [&]() {
            return static_cast<int32_t>(esp_http_client_set_header(
                client, "Content-Type", "application/json"));
          },
          [&]() {
            return static_cast<int32_t>(esp_http_client_set_header(
                client, "User-Agent", "ShotStopper/1"));
          },
          [&]() {
            return static_cast<int32_t>(esp_http_client_set_post_field(
                client, payload_, strlen(payload_)));
          }));
      if (error == ESP_OK) {
        cancelActive_.store(false, std::memory_order_release);
        activeCloseError_.store(0, std::memory_order_release);
        mux_.lock();
        activeClient_ = client;
        cancelInProgress_ = false;
        mux_.unlock();
        if (dispatchAllowed() &&
            !cancelActive_.load(std::memory_order_acquire)) {
          error = esp_http_client_perform(client);
          statusCode = esp_http_client_get_status_code(client);
          ok = error == ESP_OK && statusCode >= 200 && statusCode < 300 &&
               dispatchAllowed() &&
               !cancelActive_.load(std::memory_order_acquire);
        } else {
          error = ESP_ERR_INVALID_STATE;
        }
        mux_.lock();
        activeClient_ = nullptr;
        mux_.unlock();
        for (;;) {
          mux_.lock();
          const bool referenced = activeClientUsers_ != 0;
          mux_.unlock();
          if (!referenced) break;
          vTaskDelay(pdMS_TO_TICKS(1));
        }
        const int32_t closeError =
            activeCloseError_.exchange(0, std::memory_order_acq_rel);
        if (closeError != 0) error = static_cast<esp_err_t>(closeError);
      }
      if (!ok) {
        // Keep the allocated client/configuration, but discard any socket/TLS
        // session left by a timeout, RF cancellation or protocol failure. A
        // later event can reconnect without repeating handle allocation.
        const esp_err_t closeError = esp_http_client_close(client);
        if (closeError != ESP_OK && closeError != ESP_ERR_INVALID_STATE &&
            error == ESP_OK) {
          error = closeError;
        }
        mux_.lock();
        ++status_.transportResets;
        mux_.unlock();
      }
    }
  }

  const HeapCapSnapshot heapAfter = sampleHeapCaps();
  mux_.lock();
  status_.sending = false;
  status_.lastSuccess = ok;
  status_.lastHttpStatus = statusCode > 0 ? static_cast<uint16_t>(statusCode) : 0;
  status_.lastError = static_cast<int32_t>(error);
  if (ok) ++status_.sent;
  else ++status_.dropped;
  ++status_.heapSamples;
  status_.internalFreeBefore = heapBefore.internalFree;
  status_.internalFreeAfter = heapAfter.internalFree;
  status_.internalLargestBefore = heapBefore.internalLargest;
  status_.internalLargestAfter = heapAfter.internalLargest;
  if (status_.internalLargestMinimum == 0 ||
      heapAfter.internalLargest < status_.internalLargestMinimum) {
    status_.internalLargestMinimum = heapAfter.internalLargest;
  }
  status_.psramLargestBefore = heapBefore.psramLargest;
  status_.psramLargestAfter = heapAfter.psramLargest;
  mux_.unlock();
  return ok;
}

}  // namespace shotstopper

#endif
