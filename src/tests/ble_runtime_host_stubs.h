#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

using TickType_t = uint32_t;
using EventBits_t = uint32_t;
using esp_err_t = int;
constexpr int pdTRUE = 1, pdFALSE = 0, ESP_OK = 0, BLE_HS_ETIMEOUT = 13;
constexpr uint32_t BIT0 = 1, BIT1 = 2;
constexpr uint32_t MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_SPIRAM = 4;
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGE(...) ((void)0)
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
inline thread_local unsigned runtimeCriticalDepth = 0;
inline void portENTER_CRITICAL(portMUX_TYPE *mux) { mux->lock(); ++runtimeCriticalDepth; }
inline void portEXIT_CRITICAL(portMUX_TYPE *mux) { --runtimeCriticalDepth; mux->unlock(); }
using StaticSemaphore_t = std::timed_mutex;
using SemaphoreHandle_t = StaticSemaphore_t *;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *mutex) { return mutex; }
inline int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t timeout) {
  assert(runtimeCriticalDepth == 0);
  return mutex->try_lock_for(std::chrono::milliseconds(timeout)) ? pdTRUE : pdFALSE;
}
inline void xSemaphoreGive(SemaphoreHandle_t mutex) { mutex->unlock(); }
inline TickType_t xTaskGetTickCount() {
  return static_cast<TickType_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct StaticEventGroup_t {
  std::mutex mutex;
  std::condition_variable changed;
  EventBits_t bits = 0;
};
using EventGroupHandle_t = StaticEventGroup_t *;
inline EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *events) { return events; }
inline void xEventGroupClearBits(EventGroupHandle_t events, EventBits_t bits) {
  std::lock_guard<std::mutex> lock(events->mutex);
  events->bits &= ~bits;
}
inline void xEventGroupSetBits(EventGroupHandle_t events, EventBits_t bits) {
  std::lock_guard<std::mutex> lock(events->mutex);
  events->bits |= bits;
  events->changed.notify_all();
}
inline EventBits_t xEventGroupWaitBits(EventGroupHandle_t events, EventBits_t bits,
                                     int, int, TickType_t timeout) {
  std::unique_lock<std::mutex> lock(events->mutex);
  events->changed.wait_for(lock, std::chrono::milliseconds(timeout),
                          [&] { return (events->bits & bits) == bits; });
  return events->bits;
}
struct TestRuntimeTask { uint32_t watermark = 512; };
using TaskHandle_t = TestRuntimeTask *;
inline TaskHandle_t runtimeTask = nullptr;
inline std::thread runtimeThread;
inline std::mutex runtimeRunMutex;
inline std::condition_variable runtimeRunChanged;
inline bool runtimeExit = false;
inline bool runtimeSync = true;
inline std::atomic<unsigned> runtimeDeleted{0};
inline std::atomic<unsigned> runtimeReads{0};
inline std::function<void()> runtimeBeforeStackRead;
inline struct { void (*reset_cb)(int) = nullptr; void (*sync_cb)() = nullptr; } ble_hs_cfg;
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return runtimeTask; }
inline uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t task) {
  assert(runtimeCriticalDepth == 0);
  if (task != nullptr && runtimeBeforeStackRead) runtimeBeforeStackRead();
  ++runtimeReads;
  return (task == nullptr ? runtimeTask : task)->watermark;
}
inline void vTaskSuspend(TaskHandle_t) {}
inline esp_err_t nimble_port_init() { runtimeExit = false; return ESP_OK; }
inline esp_err_t nimble_port_deinit() { return ESP_OK; }
inline void nimble_port_run() {
  if (runtimeSync) ble_hs_cfg.sync_cb();
  std::unique_lock<std::mutex> lock(runtimeRunMutex);
  runtimeRunChanged.wait(lock, [] { return runtimeExit; });
}
inline int nimble_port_stop() {
  std::lock_guard<std::mutex> lock(runtimeRunMutex);
  runtimeExit = true;
  runtimeRunChanged.notify_all();
  return 0;
}
inline void nimble_port_freertos_init(void (*entry)(void *)) {
  runtimeTask = new TestRuntimeTask;
  runtimeThread = std::thread(entry, nullptr);
}
inline void nimble_port_freertos_deinit() {
  runtimeThread.join();
  delete runtimeTask;
  runtimeTask = nullptr;
  ++runtimeDeleted;
}
inline int ble_hs_id_infer_auto(int, uint8_t *type) { *type = 0; return 0; }
inline void ble_svc_gap_init() {}
inline void ble_svc_gatt_init() {}
inline uint32_t heap_caps_get_free_size(uint32_t) { return 1000; }
inline uint32_t heap_caps_get_minimum_free_size(uint32_t) { return 800; }
inline uint32_t heap_caps_get_largest_free_block(uint32_t) { return 600; }
