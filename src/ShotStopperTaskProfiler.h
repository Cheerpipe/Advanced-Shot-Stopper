#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

#include "ShotStopperScaleTypes.h"
#include "ShotStopperTaskMutex.h"

#if defined(ARDUINO) && !defined(SHOT_STOPPER_HOST_TEST)
#include "ShotStopperPsram.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/idf_additions.h>
#endif

namespace shotstopper {

constexpr size_t TASK_PROFILER_MAX_TRACKED = 48;
constexpr size_t TASK_PROFILER_MAX_ROWS = 20;
constexpr size_t TASK_PROFILER_NAME_CAPACITY = 24;
constexpr uint32_t TASK_PROFILER_SAMPLE_INTERVAL_MS = 1000;
constexpr uint32_t TASK_PROFILER_MAX_DURATION_MS = 5UL * 60UL * 1000UL;
constexpr uint8_t LOOP_PHASE_COUNT = 9;

enum class LoopPhase : uint8_t {
  SAFETY_HEALTH,
  SCALE_MACHINE_INPUT,
  MACHINE_GUARDS,
  CONTROL,
  ALERTS_TIMERS,
  COMMANDS,
  HOUSEKEEPING,
  DIAGNOSTICS,
  FINAL_SCALE_DRAIN
};

inline const char *loopPhaseName(LoopPhase phase) {
  switch (phase) {
    case LoopPhase::SAFETY_HEALTH: return "safety/health";
    case LoopPhase::SCALE_MACHINE_INPUT: return "scale/machine input";
    case LoopPhase::MACHINE_GUARDS: return "machine guards";
    case LoopPhase::CONTROL: return "control";
    case LoopPhase::ALERTS_TIMERS: return "alerts/timers";
    case LoopPhase::COMMANDS: return "commands";
    case LoopPhase::HOUSEKEEPING: return "housekeeping";
    case LoopPhase::DIAGNOSTICS: return "diagnostics";
    case LoopPhase::FINAL_SCALE_DRAIN: return "final scale drain";
  }
  return "unknown";
}

struct LoopPhaseProfilerRow {
  const char *name = nullptr;
  uint32_t sampleCount = 0;
  uint32_t averageExecutionUs = 0;
  uint32_t maxExecutionUs = 0;
  uint32_t lastExecutionUs = 0;
  uint32_t peakGapExecutionUs = 0;
  float currentCpuPct = 0.0f;
  float averageCpuPct = 0.0f;
};

struct LoopPhaseProfilerSnapshot {
  uint8_t rowCount = 0;
  uint32_t peakGapMs = 0;
  LoopPhaseProfilerRow rows[LOOP_PHASE_COUNT] = {};
};

class LoopPhaseProfiler {
 public:
  void beginIteration(bool enabled, uint32_t nowUs) {
    memset(iterationUs_, 0, sizeof(iterationUs_));
    if (enabled && !active_) {
      reset_(nowUs);
    } else if (!enabled && active_) {
      publish_(nowUs);
      active_ = false;
    }
  }

  void record(LoopPhase phase, uint32_t durationUs, uint32_t nowUs) {
    const uint8_t index = static_cast<uint8_t>(phase);
    if (index >= LOOP_PHASE_COUNT) return;
    iterationUs_[index] = durationUs;
    lastUs_[index] = durationUs;
    if (durationUs > maxUs_[index]) maxUs_[index] = durationUs;
    if (active_) {
      totalsUs_[index] += durationUs;
      windowUs_[index] += durationUs;
      ++sampleCounts_[index];
    }
    if (static_cast<uint32_t>(nowUs - windowStartedAtUs_) >= 1000000U) {
      publish_(nowUs);
    }
  }

  void requestReset() { resetRequested_.store(true, std::memory_order_release); }

  bool consumeReset() {
    if (!resetRequested_.exchange(false, std::memory_order_acq_rel)) return false;
    memset(maxUs_, 0, sizeof(maxUs_));
    memset(peakGapUs_, 0, sizeof(peakGapUs_));
    peakGapMs_ = 0;
    TaskLockGuard lock(snapshotMutex_);
    snapshot_.peakGapMs = 0;
    for (LoopPhaseProfilerRow &row : snapshot_.rows) {
      row.maxExecutionUs = 0;
      row.peakGapExecutionUs = 0;
    }
    return true;
  }

  void capturePeakGap(uint32_t gapMs) {
    memcpy(peakGapUs_, iterationUs_, sizeof(peakGapUs_));
    peakGapMs_ = gapMs;
  }

  void copySnapshot(LoopPhaseProfilerSnapshot &out) const {
    TaskLockGuard lock(snapshotMutex_);
    out = snapshot_;
  }

 private:
  void reset_(uint32_t nowUs) {
    memset(totalsUs_, 0, sizeof(totalsUs_));
    memset(windowUs_, 0, sizeof(windowUs_));
    memset(sampleCounts_, 0, sizeof(sampleCounts_));
    startedAtUs_ = nowUs;
    windowStartedAtUs_ = nowUs;
    active_ = true;
    publish_(nowUs);
  }

  void publish_(uint32_t nowUs) {
    const uint32_t elapsedUs = nowUs - startedAtUs_;
    const uint32_t windowElapsedUs = nowUs - windowStartedAtUs_;
    LoopPhaseProfilerSnapshot next;
    if (!active_) copySnapshot(next);
    next.rowCount = LOOP_PHASE_COUNT;
    next.peakGapMs = peakGapMs_;
    for (uint8_t i = 0; i < LOOP_PHASE_COUNT; ++i) {
      LoopPhaseProfilerRow &row = next.rows[i];
      row.name = loopPhaseName(static_cast<LoopPhase>(i));
      row.lastExecutionUs = lastUs_[i];
      row.peakGapExecutionUs = peakGapUs_[i];
      row.maxExecutionUs = maxUs_[i];
      if (!active_) continue;
      row.sampleCount = sampleCounts_[i];
      row.averageExecutionUs = sampleCounts_[i] == 0
                                   ? 0
                                   : static_cast<uint32_t>(totalsUs_[i] /
                                                           sampleCounts_[i]);
      row.currentCpuPct = windowElapsedUs == 0
                              ? 0.0f
                              : static_cast<float>(windowUs_[i]) * 100.0f /
                                    static_cast<float>(windowElapsedUs);
      row.averageCpuPct = elapsedUs == 0
                              ? 0.0f
                              : static_cast<float>(totalsUs_[i]) * 100.0f /
                                    static_cast<float>(elapsedUs);
      windowUs_[i] = 0;
    }
    windowStartedAtUs_ = nowUs;
    TaskLockGuard lock(snapshotMutex_);
    snapshot_ = next;
  }

  bool active_ = false;
  uint32_t startedAtUs_ = 0;
  uint32_t windowStartedAtUs_ = 0;
  uint64_t totalsUs_[LOOP_PHASE_COUNT] = {};
  uint32_t windowUs_[LOOP_PHASE_COUNT] = {};
  uint32_t sampleCounts_[LOOP_PHASE_COUNT] = {};
  uint32_t maxUs_[LOOP_PHASE_COUNT] = {};
  uint32_t lastUs_[LOOP_PHASE_COUNT] = {};
  uint32_t iterationUs_[LOOP_PHASE_COUNT] = {};
  uint32_t peakGapUs_[LOOP_PHASE_COUNT] = {};
  uint32_t peakGapMs_ = 0;
  std::atomic<bool> resetRequested_{false};
  mutable TaskMutex snapshotMutex_;
  LoopPhaseProfilerSnapshot snapshot_ = {};
};

enum class TaskProfilerState : uint8_t { NEVER, RUNNING, STOPPED, FAILED };
enum class TaskProfilerStopReason : uint8_t {
  NONE,
  MANUAL,
  TIMEOUT,
  ALLOCATION_FAILED,
  CAPTURE_FAILED
};

inline const char *taskProfilerStateName(TaskProfilerState state) {
  switch (state) {
    case TaskProfilerState::NEVER: return "never";
    case TaskProfilerState::RUNNING: return "running";
    case TaskProfilerState::STOPPED: return "stopped";
    case TaskProfilerState::FAILED: return "failed";
  }
  return "failed";
}

inline const char *taskProfilerStopReasonName(TaskProfilerStopReason reason) {
  switch (reason) {
    case TaskProfilerStopReason::NONE: return "none";
    case TaskProfilerStopReason::MANUAL: return "manual";
    case TaskProfilerStopReason::TIMEOUT: return "timeout";
    case TaskProfilerStopReason::ALLOCATION_FAILED: return "allocation_failed";
    case TaskProfilerStopReason::CAPTURE_FAILED: return "capture_failed";
  }
  return "capture_failed";
}

struct TaskProfilerRow {
  char name[TASK_PROFILER_NAME_CAPACITY] = {};
  uint32_t taskNumber = 0;
  int8_t core = -1;
  uint32_t stackMinBytes = UINT32_MAX;
  float currentCpuPct = 0.0f;
  float averageCpuPct = 0.0f;
};

struct TaskProfilerSnapshot {
  TaskProfilerState state = TaskProfilerState::NEVER;
  TaskProfilerStopReason stopReason = TaskProfilerStopReason::NONE;
  uint32_t elapsedMs = 0;
  uint32_t remainingMs = 0;
  uint32_t sampleCount = 0;
  uint32_t lastCaptureUs = 0;
  uint32_t maxCaptureUs = 0;
  float currentTotalCpuPct = 0.0f;
  float averageTotalCpuPct = 0.0f;
  float unreportedCurrentCpuPct = 0.0f;
  float unreportedAverageCpuPct = 0.0f;
  bool truncated = false;
  uint8_t rowCount = 0;
  TaskProfilerRow rows[TASK_PROFILER_MAX_ROWS] = {};
  LoopPhaseProfilerSnapshot loopPhases = {};
};

class TaskProfiler {
 public:
  ~TaskProfiler() { releaseWorkspace_(); }

  bool start(uint32_t nowMs) {
    if (report_.state == TaskProfilerState::RUNNING) {
      return false;
    }
#if defined(ARDUINO) && !defined(SHOT_STOPPER_HOST_TEST)
    ActiveWorkspace *next = static_cast<ActiveWorkspace *>(
        allocExternal(sizeof(ActiveWorkspace), AllocationOwner::PROFILER));
    if (next == nullptr) {
      noteStartFailure_(TaskProfilerStopReason::ALLOCATION_FAILED);
      return false;
    }
    memset(next, 0, sizeof(*next));
    // uxTaskGetSystemState writes its output while holding the kernel lock.
    next->capture = static_cast<TaskStatus_t *>(
        allocInternal(sizeof(TaskStatus_t) * TASK_PROFILER_MAX_TRACKED, AllocationOwner::PROFILER));
    if (next->capture == nullptr) {
      heapCapsFree(next);
      noteStartFailure_(TaskProfilerStopReason::ALLOCATION_FAILED);
      return false;
    }
    uint32_t ignoredTotal = 0;
    const int64_t captureStartedUs = esp_timer_get_time();
    const UBaseType_t count = uxTaskGetSystemState(
        next->capture, TASK_PROFILER_MAX_TRACKED, &ignoredTotal);
    const int64_t captureEndedUs = esp_timer_get_time();
    if (count == 0 || uxTaskGetNumberOfTasks() > TASK_PROFILER_MAX_TRACKED) {
      heapCapsFree(next->capture);
      heapCapsFree(next);
      noteStartFailure_(TaskProfilerStopReason::CAPTURE_FAILED);
      return false;
    }
    next->trackedCount = static_cast<uint8_t>(count);
    for (UBaseType_t index = 0; index < count; ++index) {
      seedTracked_(next->tracked[index], next->capture[index]);
    }
    releaseWorkspace_();
    workspace_ = next;
    beginSnapshotWrite_();
    report_ = TaskProfilerSnapshot{};
    report_.state = TaskProfilerState::RUNNING;
    startedAtMs_ = nowMs;
    startedAtUs_ = captureEndedUs;
    lastCaptureAtMs_ = nowMs;
    lastCaptureAtUs_ = captureEndedUs;
    intervalUs_ = 0;
    const uint64_t initialCost =
        captureEndedUs > captureStartedUs ? captureEndedUs - captureStartedUs : 0;
    report_.lastCaptureUs = clampU32_(initialCost);
    report_.maxCaptureUs = report_.lastCaptureUs;
    refreshReport_(nowMs);
    endSnapshotWrite_();
    loopPhaseEnabled_.store(true, std::memory_order_release);
    return true;
#else
    (void)nowMs;
    beginSnapshotWrite_();
    report_.state = TaskProfilerState::FAILED;
    report_.stopReason = TaskProfilerStopReason::CAPTURE_FAILED;
    endSnapshotWrite_();
    return false;
#endif
  }

  bool stop(uint32_t nowMs) {
    if (report_.state != TaskProfilerState::RUNNING) {
      return false;
    }
#if defined(ARDUINO) && !defined(SHOT_STOPPER_HOST_TEST)
    if (static_cast<uint32_t>(nowMs - lastCaptureAtMs_) >= 100U) {
      sample_(nowMs);
    }
    if (report_.state == TaskProfilerState::RUNNING) {
      finish_(TaskProfilerState::STOPPED, TaskProfilerStopReason::MANUAL,
              nowMs);
    }
#else
    (void)nowMs;
#endif
    return true;
  }

  void service(uint32_t nowMs) {
#if defined(ARDUINO) && !defined(SHOT_STOPPER_HOST_TEST)
    if (report_.state != TaskProfilerState::RUNNING) {
      return;
    }
    if (static_cast<uint32_t>(nowMs - startedAtMs_) >=
        TASK_PROFILER_MAX_DURATION_MS) {
      sample_(nowMs);
      if (report_.state == TaskProfilerState::RUNNING) {
        finish_(TaskProfilerState::STOPPED, TaskProfilerStopReason::TIMEOUT,
                nowMs);
      }
      return;
    }
    if (static_cast<uint32_t>(nowMs - lastCaptureAtMs_) >=
        TASK_PROFILER_SAMPLE_INTERVAL_MS) {
      sample_(nowMs);
    }
#else
    (void)nowMs;
#endif
  }

  bool running() const {
    TaskLockGuard lock(reportMutex_);
    return report_.state == TaskProfilerState::RUNNING;
  }
  bool loopPhaseProfilingEnabled() const {
    return loopPhaseEnabled_.load(std::memory_order_acquire);
  }
  TaskProfilerSnapshot snapshot() const {
    TaskProfilerSnapshot out;
    copySnapshot(out);
    return out;
  }

  void copySnapshot(TaskProfilerSnapshot &out) const {
    TaskLockGuard lock(reportMutex_);
    out = report_;
  }

#if defined(SHOT_STOPPER_HOST_TEST)
  void resetForHost() {
    TaskLockGuard lock(reportMutex_);
    report_ = TaskProfilerSnapshot{};
  }

  void publishForHost(const TaskProfilerSnapshot &next) {
    TaskLockGuard lock(reportMutex_);
    report_ = next;
  }
#endif

 private:
  void beginSnapshotWrite_() {
    reportMutex_.lock();
  }
  void endSnapshotWrite_() {
    reportMutex_.unlock();
  }
#if defined(ARDUINO) && !defined(SHOT_STOPPER_HOST_TEST)
  struct TrackedTask {
    char name[TASK_PROFILER_NAME_CAPACITY] = {};
    uint32_t taskNumber = 0;
    uint32_t previousCounter = 0;
    uint64_t accumulatedCounter = 0;
    uint32_t currentDelta = 0;
    uint32_t stackMinBytes = UINT32_MAX;
    int8_t core = -1;
    bool idle = false;
  };

  struct ActiveWorkspace {
    TaskStatus_t *capture = nullptr;
    TrackedTask tracked[TASK_PROFILER_MAX_TRACKED] = {};
    uint8_t trackedCount = 0;
  };
  static_assert(sizeof(ActiveWorkspace) <= 4096,
                "Profiler external workspace exceeds its budget");
  static_assert(sizeof(TaskStatus_t) * TASK_PROFILER_MAX_TRACKED <= 4096,
                "Profiler kernel capture exceeds its internal budget");

  static uint32_t clampU32_(uint64_t value) {
    return value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(value);
  }

  void noteStartFailure_(TaskProfilerStopReason reason) {
    loopPhaseEnabled_.store(false, std::memory_order_release);
    beginSnapshotWrite_();
    if (report_.state == TaskProfilerState::NEVER) {
      report_.state = TaskProfilerState::FAILED;
      report_.stopReason = reason;
    }
    endSnapshotWrite_();
  }

  static bool idleTaskName_(const char *name) {
    return name != nullptr &&
           (strcmp(name, "IDLE0") == 0 || strcmp(name, "IDLE1") == 0 ||
            strcmp(name, "IDLE") == 0);
  }

  static int8_t coreFor_(BaseType_t core) {
    return core == 0 || core == 1 ? static_cast<int8_t>(core) : -1;
  }

  // TaskStatus_t only has a core field when FREERTOS_VTASKLIST_INCLUDE_COREID
  // is on. xTaskGetCoreID is always present on ESP-IDF.
  static int8_t coreOf_(const TaskStatus_t &task) {
    return coreFor_(xTaskGetCoreID(task.xHandle));
  }

  static void seedTracked_(TrackedTask &tracked, const TaskStatus_t &task) {
    tracked = TrackedTask{};
    tracked.taskNumber = static_cast<uint32_t>(task.xTaskNumber);
    tracked.previousCounter = static_cast<uint32_t>(task.ulRunTimeCounter);
    tracked.stackMinBytes = static_cast<uint32_t>(task.usStackHighWaterMark);
    tracked.core = coreOf_(task);
    if (task.pcTaskName != nullptr) {
      copyCString(tracked.name, sizeof(tracked.name), task.pcTaskName);
    }
    tracked.idle = idleTaskName_(tracked.name);
  }

  TrackedTask *findTracked_(uint32_t taskNumber) {
    for (uint8_t index = 0; index < workspace_->trackedCount; ++index) {
      if (workspace_->tracked[index].taskNumber == taskNumber) {
        return &workspace_->tracked[index];
      }
    }
    return nullptr;
  }

  void sample_(uint32_t nowMs) {
    if (workspace_ == nullptr) {
      finish_(TaskProfilerState::FAILED,
              TaskProfilerStopReason::CAPTURE_FAILED, nowMs);
      return;
    }
    uint32_t ignoredTotal = 0;
    const int64_t captureStartedUs = esp_timer_get_time();
    const UBaseType_t count = uxTaskGetSystemState(
        workspace_->capture, TASK_PROFILER_MAX_TRACKED, &ignoredTotal);
    const int64_t captureEndedUs = esp_timer_get_time();
    if (count == 0) {
      finish_(TaskProfilerState::FAILED,
              TaskProfilerStopReason::CAPTURE_FAILED, nowMs);
      return;
    }
    const uint64_t intervalUs = captureEndedUs > lastCaptureAtUs_
                                    ? captureEndedUs - lastCaptureAtUs_
                                    : 0;
    if (intervalUs == 0) {
      return;
    }
    for (uint8_t index = 0; index < workspace_->trackedCount; ++index) {
      workspace_->tracked[index].currentDelta = 0;
    }
    bool truncated = uxTaskGetNumberOfTasks() > TASK_PROFILER_MAX_TRACKED;
    for (UBaseType_t index = 0; index < count; ++index) {
      const TaskStatus_t &task = workspace_->capture[index];
      const uint32_t taskNumber = static_cast<uint32_t>(task.xTaskNumber);
      TrackedTask *tracked = findTracked_(taskNumber);
      if (tracked == nullptr) {
        if (workspace_->trackedCount >= TASK_PROFILER_MAX_TRACKED) {
          truncated = true;
          continue;
        }
        tracked = &workspace_->tracked[workspace_->trackedCount++];
        seedTracked_(*tracked, task);
        continue;
      }
      const uint32_t counter = static_cast<uint32_t>(task.ulRunTimeCounter);
      const uint32_t delta = counter - tracked->previousCounter;
      tracked->previousCounter = counter;
      tracked->currentDelta = delta;
      tracked->accumulatedCounter += delta;
      const uint32_t stackMin =
          static_cast<uint32_t>(task.usStackHighWaterMark);
      if (stackMin < tracked->stackMinBytes) {
        tracked->stackMinBytes = stackMin;
      }
      tracked->core = coreOf_(task);
    }
    intervalUs_ = intervalUs;
    lastCaptureAtUs_ = captureEndedUs;
    lastCaptureAtMs_ = nowMs;
    beginSnapshotWrite_();
    ++report_.sampleCount;
    report_.truncated = report_.truncated || truncated;
    report_.lastCaptureUs = clampU32_(
        captureEndedUs > captureStartedUs ? captureEndedUs - captureStartedUs
                                          : 0);
    if (report_.lastCaptureUs > report_.maxCaptureUs) {
      report_.maxCaptureUs = report_.lastCaptureUs;
    }
    refreshReport_(nowMs);
    endSnapshotWrite_();
  }

  void refreshReport_(uint32_t nowMs) {
    if (workspace_ == nullptr) {
      return;
    }
    report_.elapsedMs = static_cast<uint32_t>(nowMs - startedAtMs_);
    report_.remainingMs =
        report_.elapsedMs >= TASK_PROFILER_MAX_DURATION_MS
            ? 0U
            : TASK_PROFILER_MAX_DURATION_MS - report_.elapsedMs;
    report_.currentTotalCpuPct = 0.0f;
    report_.averageTotalCpuPct = 0.0f;
    report_.unreportedCurrentCpuPct = 0.0f;
    report_.unreportedAverageCpuPct = 0.0f;
    report_.rowCount = 0;
    memset(report_.rows, 0, sizeof(report_.rows));

    const uint64_t elapsedUs = lastCaptureAtUs_ > startedAtUs_
                                   ? lastCaptureAtUs_ - startedAtUs_
                                   : 0;
    for (uint8_t index = 0; index < workspace_->trackedCount; ++index) {
      const TrackedTask &tracked = workspace_->tracked[index];
      if (tracked.idle) {
        continue;
      }
      TaskProfilerRow row;
      copyCString(row.name, sizeof(row.name), tracked.name);
      row.taskNumber = tracked.taskNumber;
      row.core = tracked.core;
      row.stackMinBytes = tracked.stackMinBytes;
      row.currentCpuPct = intervalUs_ == 0
                              ? 0.0f
                              : static_cast<float>(tracked.currentDelta) *
                                    100.0f / static_cast<float>(intervalUs_);
      row.averageCpuPct = elapsedUs == 0
                              ? 0.0f
                              : static_cast<float>(tracked.accumulatedCounter) *
                                    100.0f / static_cast<float>(elapsedUs);
      report_.currentTotalCpuPct += row.currentCpuPct;
      report_.averageTotalCpuPct += row.averageCpuPct;

      const float rank = report_.state == TaskProfilerState::RUNNING
                             ? row.currentCpuPct
                             : row.averageCpuPct;
      size_t insertAt = report_.rowCount;
      while (insertAt > 0) {
        const TaskProfilerRow &previous = report_.rows[insertAt - 1];
        const float previousRank =
            report_.state == TaskProfilerState::RUNNING
                ? previous.currentCpuPct
                : previous.averageCpuPct;
        if (previousRank >= rank) {
          break;
        }
        --insertAt;
      }
      if (insertAt >= TASK_PROFILER_MAX_ROWS) {
        continue;
      }
      const size_t moveEnd = report_.rowCount < TASK_PROFILER_MAX_ROWS
                                 ? report_.rowCount
                                 : TASK_PROFILER_MAX_ROWS - 1;
      for (size_t move = moveEnd; move > insertAt; --move) {
        report_.rows[move] = report_.rows[move - 1];
      }
      report_.rows[insertAt] = row;
      if (report_.rowCount < TASK_PROFILER_MAX_ROWS) {
        ++report_.rowCount;
      }
    }
    float shownCurrent = 0.0f;
    float shownAverage = 0.0f;
    for (uint8_t index = 0; index < report_.rowCount; ++index) {
      shownCurrent += report_.rows[index].currentCpuPct;
      shownAverage += report_.rows[index].averageCpuPct;
    }
    report_.unreportedCurrentCpuPct =
        report_.currentTotalCpuPct > shownCurrent
            ? report_.currentTotalCpuPct - shownCurrent
            : 0.0f;
    report_.unreportedAverageCpuPct =
        report_.averageTotalCpuPct > shownAverage
            ? report_.averageTotalCpuPct - shownAverage
            : 0.0f;
    if (workspace_->trackedCount > TASK_PROFILER_MAX_ROWS) {
      report_.truncated = true;
    }
  }

  void finish_(TaskProfilerState state, TaskProfilerStopReason reason,
               uint32_t nowMs) {
    loopPhaseEnabled_.store(false, std::memory_order_release);
    beginSnapshotWrite_();
    refreshReport_(nowMs);
    report_.state = state;
    report_.stopReason = reason;
    report_.remainingMs = 0;
    // Stopped reports are easier to read when ranked by the session average.
    if (workspace_ != nullptr) {
      refreshReport_(nowMs);
    }
    endSnapshotWrite_();
    releaseWorkspace_();
  }

  void releaseWorkspace_() {
    if (workspace_ != nullptr) {
      heapCapsFree(workspace_->capture);
      heapCapsFree(workspace_);
      workspace_ = nullptr;
    }
  }

  ActiveWorkspace *workspace_ = nullptr;
  uint64_t startedAtUs_ = 0;
  uint64_t lastCaptureAtUs_ = 0;
  uint64_t intervalUs_ = 0;
  uint32_t startedAtMs_ = 0;
  uint32_t lastCaptureAtMs_ = 0;
#else
  void releaseWorkspace_() {}
#endif

  TaskProfilerSnapshot report_ = {};
  std::atomic<bool> loopPhaseEnabled_{false};
  mutable TaskMutex reportMutex_;
};

}  // namespace shotstopper
