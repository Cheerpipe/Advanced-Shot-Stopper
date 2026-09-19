#include "ShotStopperBleArbiter.h"

#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#else
#include <mutex>
#endif

namespace {

struct ObserverSlot {
  ShotStopperBleAdvertisementObserver observer = nullptr;
  void *context = nullptr;
};

struct State {
  ObserverSlot observers[2];
  ShotStopperBleOwner owner = ShotStopperBleOwner::None;
  uint32_t nextLeaseId = 0;
  uint32_t leaseId = 0;
  uint32_t epoch = 1;
  uint32_t criticalEpoch = 0;
  uint32_t scaleReservations = 0;
  uint32_t machinePreemptions = 0;
  uint32_t machineDenials = 0;
  bool observersSealed = false;
  bool critical = false;
  bool scaleReserved = false;
};

#if defined(ESP_PLATFORM)
portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;
#else
std::mutex gLock;
#endif
State gState;

class Lock {
 public:
  Lock() {
#if defined(ESP_PLATFORM)
    portENTER_CRITICAL(&gLock);
#else
    gLock.lock();
#endif
  }
  ~Lock() {
#if defined(ESP_PLATFORM)
    portEXIT_CRITICAL(&gLock);
#else
    gLock.unlock();
#endif
  }
};

ObserverSlot *slotFor(ShotStopperBleOwner owner) {
  if (owner == ShotStopperBleOwner::Scale) return &gState.observers[0];
  if (owner == ShotStopperBleOwner::Machine) return &gState.observers[1];
  return nullptr;
}

void advanceEpoch() {
  if (++gState.epoch == 0) gState.epoch = 1;
}

}  // namespace

bool shotStopperBleArbiterRegisterObserver(
    ShotStopperBleOwner owner, ShotStopperBleAdvertisementObserver observer,
    void *context) {
  if (observer == nullptr) return false;
  Lock lock;
  ObserverSlot *slot = slotFor(owner);
  if (slot == nullptr || gState.observersSealed || slot->observer != nullptr) {
    return false;
  }
  *slot = {observer, context};
  return true;
}

void shotStopperBleArbiterSealObservers() {
  Lock lock;
  gState.observersSealed = true;
}

void shotStopperBleArbiterPublishAdvertisement(
    const ShotStopperBleAdvertisement &advertisement) {
  ObserverSlot observers[2];
  {
    Lock lock;
    observers[0] = gState.observers[0];
    observers[1] = gState.observers[1];
  }
  for (const ObserverSlot &slot : observers) {
    if (slot.observer != nullptr) slot.observer(advertisement, slot.context);
  }
}

void shotStopperBleArbiterReserveScaleCandidate() {
  Lock lock;
  if (!gState.scaleReserved) {
    gState.scaleReserved = true;
    ++gState.scaleReservations;
  }
  if (gState.owner == ShotStopperBleOwner::Machine) {
    advanceEpoch();
    ++gState.machinePreemptions;
  }
}

void shotStopperBleArbiterClearScaleReservation() {
  Lock lock;
  gState.scaleReserved = false;
}

bool shotStopperBleArbiterTryAcquire(ShotStopperBleOwner owner,
                                    ShotStopperBleLease &lease) {
  lease = {};
  if (owner == ShotStopperBleOwner::None) return false;
  Lock lock;
  if (gState.owner != ShotStopperBleOwner::None ||
      (owner == ShotStopperBleOwner::Machine &&
       (gState.critical || gState.scaleReserved))) {
    if (owner == ShotStopperBleOwner::Machine) ++gState.machineDenials;
    return false;
  }
  if (++gState.nextLeaseId == 0) ++gState.nextLeaseId;
  gState.owner = owner;
  gState.leaseId = gState.nextLeaseId;
  lease = {owner, gState.leaseId, gState.epoch};
  return true;
}

bool shotStopperBleArbiterLeaseCurrent(const ShotStopperBleLease &lease) {
  Lock lock;
  return lease.owner != ShotStopperBleOwner::None &&
         lease.owner == gState.owner && lease.id == gState.leaseId &&
         lease.epoch == gState.epoch &&
         (lease.owner != ShotStopperBleOwner::Machine ||
          (!gState.critical && !gState.scaleReserved));
}

void shotStopperBleArbiterRelease(const ShotStopperBleLease &lease) {
  Lock lock;
  if (lease.owner == gState.owner && lease.id == gState.leaseId) {
    gState.owner = ShotStopperBleOwner::None;
    gState.leaseId = 0;
  }
}

void shotStopperBleArbiterSetCritical(bool critical) {
  Lock lock;
  if (critical && !gState.critical) {
    ++gState.criticalEpoch;
    advanceEpoch();
    if (gState.owner == ShotStopperBleOwner::Machine) {
      ++gState.machinePreemptions;
    }
  }
  gState.critical = critical;
}

ShotStopperBleArbiterSnapshot shotStopperBleArbiterSnapshot() {
  Lock lock;
  return {gState.owner,
          gState.epoch,
          gState.criticalEpoch,
          gState.scaleReservations,
          gState.machinePreemptions,
          gState.machineDenials,
          gState.critical,
          gState.scaleReserved};
}

void shotStopperBleArbiterInvalidateAll() {
  Lock lock;
  advanceEpoch();
  gState.owner = ShotStopperBleOwner::None;
  gState.leaseId = 0;
  gState.scaleReserved = false;
  gState.critical = false;
}

void shotStopperBleArbiterResetForTest() {
  Lock lock;
  gState = {};
  gState.epoch = 1;
}
