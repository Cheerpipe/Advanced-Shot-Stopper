#pragma once
// Deterministic scheduler/transport boundary for the production webhook worker.
// No sockets, hardware, implicit tasks, or wall-clock sleeps.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <vector>

using BaseType_t = int;
using TickType_t = uint32_t;
constexpr int pdTRUE = 1, pdPASS = 1, tskIDLE_PRIORITY = 0;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
inline TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
struct TestSemaphore { unsigned count; };
using SemaphoreHandle_t = TestSemaphore *;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new TestSemaphore{1}; }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return new TestSemaphore{0}; }
inline int xSemaphoreTake(SemaphoreHandle_t s, TickType_t) {
  if (!s->count) return 0;
  --s->count;
  return pdTRUE;
}
inline int xSemaphoreGive(SemaphoreHandle_t s) { ++s->count; return pdTRUE; }
inline void vSemaphoreDelete(SemaphoreHandle_t s) { delete s; }
struct TestQueue {
  size_t depth, itemSize;
  std::deque<std::vector<uint8_t>> items;
};
struct StaticQueue_t {};
using QueueHandle_t = TestQueue *;
using TaskHandle_t = void *;
inline std::vector<std::string> workerTrace;
inline std::function<void()> afterReceive, afterTimeout, afterDelay, duringPerform;
inline uint32_t workerNow = 0;
inline QueueHandle_t xQueueCreateStatic(size_t depth, size_t size, uint8_t *, StaticQueue_t *) {
  return new TestQueue{depth, size, {}};
}
inline int xQueueSend(QueueHandle_t q, const void *item, TickType_t) {
  if (q->items.size() == q->depth) return 0;
  const auto *bytes = static_cast<const uint8_t *>(item);
  q->items.emplace_back(bytes, bytes + q->itemSize);
  return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks) {
  workerTrace.push_back("receive");
  if (q->items.empty()) {
    workerNow += ticks;
    if (afterTimeout) afterTimeout();
    return 0;
  }
  memcpy(item, q->items.front().data(), q->itemSize);
  q->items.pop_front();
  if (afterReceive) afterReceive();
  return pdTRUE;
}
inline unsigned uxQueueMessagesWaiting(QueueHandle_t q) { return q->items.size(); }
inline void vQueueDelete(QueueHandle_t q) { delete q; }
inline int xTaskCreatePinnedToCore(void (*)(void *), const char *, unsigned, void *,
                                    int, TaskHandle_t *task, int) {
  *task = reinterpret_cast<void *>(1);
  return pdPASS;
}
inline void xTaskNotifyGive(TaskHandle_t) {}
inline void vTaskDelay(TickType_t ticks) {
  workerTrace.push_back("delay");
  workerNow += ticks;
  if (afterDelay) afterDelay();
}
inline void vTaskDelete(TaskHandle_t) {}
inline uint32_t millis() { return workerNow; }
constexpr int WL_CONNECTED = 1;
inline int wifiStatus = WL_CONNECTED;
struct TestWifi { int status() const { return wifiStatus; } };
inline TestWifi WiFi;
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_STATE = -2;
constexpr int ESP_MAC_WIFI_STA = 0, HTTP_METHOD_POST = 1;
constexpr int HTTP_EVENT_ERROR = 0, HTTP_EVENT_DISCONNECTED = 1;
struct TestHttpClient {};
using esp_http_client_handle_t = TestHttpClient *;
struct esp_http_client_event_t {
  void *user_data;
  int event_id;
  esp_http_client_handle_t client;
};
struct esp_http_client_config_t {
  const char *url = nullptr;
  int timeout_ms = 0;
  bool disable_auto_redirect = false;
  void *user_data = nullptr;
  esp_err_t (*event_handler)(esp_http_client_event_t *) = nullptr;
};
inline bool failHttpAllocation = false;
inline int httpResult = ESP_OK, httpStatus = 200;
inline esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *) {
  workerTrace.push_back("allocate");
  return failHttpAllocation ? nullptr : new TestHttpClient{};
}
inline int esp_http_client_cleanup(esp_http_client_handle_t c) { delete c; return ESP_OK; }
inline int esp_http_client_close(esp_http_client_handle_t) { return ESP_OK; }
inline int esp_http_client_cancel_request(esp_http_client_handle_t) { return ESP_OK; }
inline int esp_http_client_set_method(esp_http_client_handle_t, int) { return ESP_OK; }
inline int esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *) { return ESP_OK; }
inline int esp_http_client_set_post_field(esp_http_client_handle_t, const char *, int) { return ESP_OK; }
inline int esp_http_client_perform(esp_http_client_handle_t) {
  workerTrace.push_back("perform");
  if (duringPerform) duringPerform();
  return httpResult;
}
inline int esp_http_client_get_status_code(esp_http_client_handle_t) { return httpStatus; }
inline int esp_read_mac(uint8_t *mac, int) { memset(mac, 0xAB, 6); return ESP_OK; }
