#include "ShotStopperTaskMutex.h"

// Host builds embed TaskMutex in host-test fragments; only the firmware
// target compiles this file (registered in idf/components/shotStopper).
#if !defined(SHOT_STOPPER_TASK_MUTEX_HOST)

namespace shotstopper {

void TaskMutex::lock_() {
  const BaseType_t taken = xSemaphoreTake(handle_, portMAX_DELAY);
  configASSERT(taken == pdTRUE);
  (void)taken;
  ownedSinceUs_ = static_cast<uint32_t>(esp_timer_get_time());
}

bool TaskMutex::tryLock_() {
  if (xSemaphoreTake(handle_, 0) != pdTRUE) return false;
  ownedSinceUs_ = static_cast<uint32_t>(esp_timer_get_time());
  return true;
}

void TaskMutex::unlock_() {
  // Snapshot telemetry before releasing: the fields are only touched while
  // the mutex is held, so the mutex itself serializes every access. The
  // name is copied here because the owner handle would dangle for tasks
  // deleted after giving (e.g. the websocket client's worker).
  const char *name = pcTaskGetName(nullptr);
  size_t index = 0;
  for (; name[index] != '\0' && index < sizeof(lastOwner_) - 1; ++index) {
    lastOwner_[index] = name[index];
  }
  lastOwner_[index] = '\0';
  lastHoldUs_ = static_cast<uint32_t>(esp_timer_get_time()) - ownedSinceUs_;
  // FreeRTOS implements xSemaphoreGive as a C macro with an internal handle
  // cast; the public handle type is the one created above.
  const BaseType_t given = xSemaphoreGive(handle_);
  configASSERT(given == pdTRUE);
  (void)given;
}

}  // namespace shotstopper

#endif
