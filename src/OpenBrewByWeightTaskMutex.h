#pragma once

// Snapshot payloads are ordinary C++ objects.  They must be protected by a
// real task-level synchronization primitive; sequence counters and memory
// barriers alone do not make concurrent struct copies race-free.

#if defined(SHOT_STOPPER_HOST_TEST) || \
    defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#define SHOT_STOPPER_TASK_MUTEX_HOST 1
#include <mutex>
#else
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

namespace shotstopper {

class TaskMutex {
 public:
#if defined(SHOT_STOPPER_TASK_MUTEX_HOST)
  // Per-thread observer for production-path lock-order/count assertions.
  inline static thread_local void (*hostObserver)(const TaskMutex *, bool) = nullptr;
#endif
  TaskMutex() {
#if !defined(SHOT_STOPPER_TASK_MUTEX_HOST)
    handle_ = xSemaphoreCreateMutexStatic(&storage_);
    configASSERT(handle_ != nullptr);
#endif
  }

  TaskMutex(const TaskMutex &) = delete;
  TaskMutex &operator=(const TaskMutex &) = delete;

  void lock() {
#if defined(SHOT_STOPPER_TASK_MUTEX_HOST)
    mutex_.lock();
    if (hostObserver != nullptr) hostObserver(this, true);
#else
    lock_();
#endif
  }

  // Non-blocking acquire for owners that must never wait behind the holder.
  bool tryLock() {
#if defined(SHOT_STOPPER_TASK_MUTEX_HOST)
    if (!mutex_.try_lock()) return false;
    if (hostObserver != nullptr) hostObserver(this, true);
    return true;
#else
    return tryLock_();
#endif
  }

  void unlock() {
#if defined(SHOT_STOPPER_TASK_MUTEX_HOST)
    if (hostObserver != nullptr) hostObserver(this, false);
    mutex_.unlock();
#else
    unlock_();
#endif
  }

  // Longest-hold attribution since resetLongestHold(), valid only while the
  // caller holds the mutex (see the out-of-line unlock in
  // OpenBrewByWeightTaskMutex.cpp). Host builds carry no kernel task names.
  const char *longestHoldOwnerName() const {
#if defined(SHOT_STOPPER_TASK_MUTEX_HOST)
    return "host";
#else
    return longestOwner_;
#endif
  }
  uint32_t longestHoldUs() const {
#if defined(SHOT_STOPPER_TASK_MUTEX_HOST)
    return 0;
#else
    return longestHoldUs_;
#endif
  }
  void resetLongestHold() {
#if !defined(SHOT_STOPPER_TASK_MUTEX_HOST)
    longestHoldUs_ = 0;
#endif
  }

 private:
#if defined(SHOT_STOPPER_TASK_MUTEX_HOST)
  std::mutex mutex_;
#else
  // Out-of-line: with the telemetry inside, inlining these at every
  // TaskLockGuard site costs ~25 KB of flash.
  void lock_();
  bool tryLock_();
  void unlock_();
  StaticSemaphore_t storage_ = {};
  SemaphoreHandle_t handle_ = nullptr;
  uint32_t ownedSinceUs_ = 0;
  uint32_t longestHoldUs_ = 0;
  char longestOwner_[16] = {};
#endif
};

class TaskLockGuard {
 public:
  explicit TaskLockGuard(TaskMutex &mutex) : mutex_(mutex) { mutex_.lock(); }
  ~TaskLockGuard() {
    if (locked_) {
      mutex_.unlock();
    }
  }

  void unlock() {
    if (locked_) {
      mutex_.unlock();
      locked_ = false;
    }
  }

  TaskLockGuard(const TaskLockGuard &) = delete;
  TaskLockGuard &operator=(const TaskLockGuard &) = delete;

 private:
  TaskMutex &mutex_;
  bool locked_ = true;
};

}  // namespace shotstopper
