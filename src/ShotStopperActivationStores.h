#pragma once

// Data layer for the activation stores: the stats shot log, its flash curve
// sidecar, and the independent activation history. One explicit owner for the
// three PSRAM ring stores and their deferred flash persistence.
//
// Concurrency contract:
// 1. Every read or write of shotLog/shotCurves/historyLog (append in the
//    control loop, HTTP queries and mutations, deferred flush, delete/clear)
//    runs under the caller's TaskLockGuard(shotStoreMutex). This component
//    takes no hidden internal locks, so it can never recurse into that mutex.
// 2. The persistence worker copies an immutable image under that mutex, then
//    owns its stepped flash transaction after releasing the mutex.
// 3. No FreeRTOS task or ISR is created here. Live stores are touched only
//    from control and network through the mutex; the worker touches its image.
// 4. Flash reads/writes copy through the internal-SRAM FlashIoScratch while
//    a flash path may still disable cache despite XIP; PSRAM is not passed to
//    partition I/O. Multi-word records are only read or rewritten under the
//    store mutex, so readers never observe a torn record.
// 5. begin() completes all loads before the network task serves endpoints;
//    callers invoke it under the store mutex for uniformity.

#include "ShotStopperDomain.h"
#include "ShotStopperFlashIoScratch.h"
#include "ShotStopperHistory.h"
#include "ShotStopperShotCurve.h"
#include "ShotStopperShotLog.h"

namespace shotstopper {

// Latched SHOT_LOG_PERSIST_FAILED emission stays with the component so every
// store shares one wiring; the scheduler (retry/backoff) stays with the
// control-loop servant that calls service().
using ActivationStoreEvent =
    void (*)(DebugCategory category, DebugCode code, int32_t argument1,
             int32_t argument2);

struct ActivationStoresFlushReport {
  bool anyFail = false;
  bool anyIoFail = false;
  bool complete = true;
};

class ActivationStores {
 public:
  // Boot load. Returns whether the durable boot-identity write succeeded
  // (always true in host tests). Call before the network task serves
  // endpoints; lastShotStore is loaded separately by the platform boot.
  bool begin() {
    bool bootIdentityDurable = true;
    shotLog.load();
    shotLog.onBoot();
#if !defined(SHOT_STOPPER_HOST_TEST)
    bootIdentityDurable = shotLog.save();
#endif
    shotCurves.load();
    historyLog.load();
    return bootIdentityDurable;
  }

  // Deferred flush of the three owned stores. Caller holds the store mutex,
  // passes the control-loop try-lock budget (0 ms from the 1 ms loop) and the
  // debug-event sink; last-shot NVS flushing stays with the caller.
  ActivationStoresFlushReport service(uint32_t tryLockMs,
                                      ActivationStoreEvent emit) {
    ActivationStoresFlushReport report;
    flushOne(shotLog, shotLogPersistFailLatched_, tryLockMs, emit, 0, report);
    flushOne(shotCurves, shotCurvePersistFailLatched_, tryLockMs, emit, 1,
             report);
    flushOne(historyLog, historyPersistFailLatched_, tryLockMs, emit, 3,
             report);
    return report;
  }

  // Advance at most one erase/program operation. This keeps every cache-off
  // window bounded and lets the worker re-check safety gates between steps.
  ActivationStoresFlushReport serviceStep(uint32_t tryLockMs,
                                           ActivationStoreEvent emit) {
    ActivationStoresFlushReport report;
    while (persistCursor_ < 3) {
      FlashStoreStepResult result = FlashStoreStepResult::COMPLETE;
      bool *latched = nullptr;
      bool hadDirty = false;
      int32_t discriminator = 0;
      size_t count = 0;
      const uint32_t lockTimeoutsBefore = flashIoLockTimeouts();
      if (persistCursor_ == 0) {
        hadDirty = shotLog.dirty();
        result = shotLog.flushStep(tryLockMs);
        latched = &shotLogPersistFailLatched_;
        count = shotLog.count();
      } else if (persistCursor_ == 1) {
        hadDirty = shotCurves.dirty();
        result = shotCurves.flushStep(tryLockMs);
        latched = &shotCurvePersistFailLatched_;
        discriminator = 1;
        count = shotCurves.count();
      } else {
        hadDirty = historyLog.dirty();
        result = historyLog.flushStep(tryLockMs);
        latched = &historyPersistFailLatched_;
        discriminator = 3;
        count = historyLog.count();
      }
      if (result == FlashStoreStepResult::FAILED) {
        report.anyFail = true;
        report.anyIoFail = flashIoLockTimeouts() == lockTimeoutsBefore;
        if (!*latched && emit != nullptr) {
          *latched = true;
          emit(DebugCategory::CONFIG, DebugCode::SHOT_LOG_PERSIST_FAILED,
               static_cast<int32_t>(count), discriminator);
        }
        persistCursor_ = 0;
        return report;
      }
      if (result == FlashStoreStepResult::MORE) {
        report.complete = false;
        return report;
      }
      *latched = false;
      ++persistCursor_;
      if (persistCursor_ < 3 && hadDirty) {
        report.complete = false;
        return report;
      }
    }
    persistCursor_ = 0;
    return report;
  }

  ShotLog shotLog;
  ShotCurveLog shotCurves;
  HistoryLog historyLog;

  // Harness support: reset and inspect the latched failure dedup state.
  void clearPersistFailLatches() {
    shotLogPersistFailLatched_ = false;
    shotCurvePersistFailLatched_ = false;
    historyPersistFailLatched_ = false;
  }

  bool anyPersistFailLatched() const {
    return shotLogPersistFailLatched_ || shotCurvePersistFailLatched_ ||
           historyPersistFailLatched_;
  }

  void acknowledgePersisted(const ActivationStores &image, bool clearDirty) {
    shotLog.acknowledgePersisted(image.shotLog, clearDirty);
    shotCurves.acknowledgePersisted(image.shotCurves, clearDirty);
    historyLog.acknowledgePersisted(image.historyLog, clearDirty);
    if (clearDirty) {
      shotLogPersistFailLatched_ = image.shotLogPersistFailLatched_;
      shotCurvePersistFailLatched_ = image.shotCurvePersistFailLatched_;
      historyPersistFailLatched_ = image.historyPersistFailLatched_;
    }
  }

 private:
  template <typename Log>
  void flushOne(Log &log, bool &latched, uint32_t tryLockMs,
                ActivationStoreEvent emit, int32_t discriminator,
                ActivationStoresFlushReport &report) {
    if (!log.dirty()) {
      return;
    }
    const uint32_t lockTimeoutsBefore = flashIoLockTimeouts();
    if (log.flush(tryLockMs)) {
      latched = false;
      return;
    }
    report.anyFail = true;
    report.anyIoFail |= flashIoLockTimeouts() == lockTimeoutsBefore;
    if (!latched) {
      latched = true;
      if (emit != nullptr) {
        emit(DebugCategory::CONFIG, DebugCode::SHOT_LOG_PERSIST_FAILED,
             static_cast<int32_t>(log.count()), discriminator);
      }
    }
  }

  bool shotLogPersistFailLatched_ = false;
  bool shotCurvePersistFailLatched_ = false;
  bool historyPersistFailLatched_ = false;
  uint8_t persistCursor_ = 0;
};

}  // namespace shotstopper
