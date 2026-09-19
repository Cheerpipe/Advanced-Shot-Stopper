#include "ShotStopperBleArbiter.h"

#include <cassert>

namespace {

void observe(const ShotStopperBleAdvertisement &, void *context) {
  ++*static_cast<unsigned *>(context);
}

}  // namespace

int main() {
  shotStopperBleArbiterResetForTest();
  unsigned scaleObservations = 0;
  unsigned machineObservations = 0;
  assert(shotStopperBleArbiterRegisterObserver(
      ShotStopperBleOwner::Scale, observe, &scaleObservations));
  assert(shotStopperBleArbiterRegisterObserver(
      ShotStopperBleOwner::Machine, observe, &machineObservations));
  assert(!shotStopperBleArbiterRegisterObserver(
      ShotStopperBleOwner::Machine, observe, &machineObservations));
  shotStopperBleArbiterPublishAdvertisement({});
  assert(scaleObservations == 1 && machineObservations == 1);
  shotStopperBleArbiterSealObservers();
  assert(!shotStopperBleArbiterRegisterObserver(
      ShotStopperBleOwner::Scale, observe, &scaleObservations));

  ShotStopperBleLease machine;
  assert(shotStopperBleArbiterTryAcquire(ShotStopperBleOwner::Machine,
                                         machine));
  assert(shotStopperBleArbiterLeaseCurrent(machine));
  shotStopperBleArbiterReserveScaleCandidate();
  assert(!shotStopperBleArbiterLeaseCurrent(machine));
  auto state = shotStopperBleArbiterSnapshot();
  assert(state.scaleReserved && state.machinePreemptions == 1 &&
         state.owner == ShotStopperBleOwner::Machine);
  shotStopperBleArbiterRelease(machine);

  ShotStopperBleLease denied;
  assert(!shotStopperBleArbiterTryAcquire(ShotStopperBleOwner::Machine,
                                          denied));
  ShotStopperBleLease scale;
  assert(shotStopperBleArbiterTryAcquire(ShotStopperBleOwner::Scale, scale));
  assert(shotStopperBleArbiterLeaseCurrent(scale));
  shotStopperBleArbiterRelease(scale);
  shotStopperBleArbiterClearScaleReservation();

  assert(shotStopperBleArbiterTryAcquire(ShotStopperBleOwner::Machine,
                                         machine));
  shotStopperBleArbiterSetCritical(true);
  assert(!shotStopperBleArbiterLeaseCurrent(machine));
  shotStopperBleArbiterRelease(machine);
  assert(!shotStopperBleArbiterTryAcquire(ShotStopperBleOwner::Machine,
                                          denied));
  assert(shotStopperBleArbiterTryAcquire(ShotStopperBleOwner::Scale, scale));
  shotStopperBleArbiterRelease(scale);
  shotStopperBleArbiterSetCritical(false);
  assert(shotStopperBleArbiterTryAcquire(ShotStopperBleOwner::Machine,
                                         machine));
  shotStopperBleArbiterInvalidateAll();
  assert(!shotStopperBleArbiterLeaseCurrent(machine));
  assert(shotStopperBleArbiterSnapshot().owner == ShotStopperBleOwner::None);
  return 0;
}
