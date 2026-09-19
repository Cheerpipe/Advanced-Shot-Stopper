#pragma once

#include <cstddef>
#include <cstdint>

enum class ShotStopperBleOwner : uint8_t { None, Scale, Machine };
struct ShotStopperBleLease {
  ShotStopperBleOwner owner = ShotStopperBleOwner::None;
  uint32_t id = 0;
  uint32_t epoch = 0;
};
struct ShotStopperBleAdvertisement {
  uint8_t addressType = 0;
  uint8_t address[6] = {};
  int8_t rssi = 0;
  bool connectable = false;
  const uint8_t *payload = nullptr;
  uint8_t payloadLength = 0;
};
struct ShotStopperBleArbiterSnapshot {
  ShotStopperBleOwner owner = ShotStopperBleOwner::None;
  uint32_t epoch = 0;
  uint32_t criticalEpoch = 0;
  uint32_t scaleReservations = 0;
  uint32_t machinePreemptions = 0;
  uint32_t machineDenials = 0;
  bool critical = false;
  bool scaleReserved = false;
};
using ShotStopperBleAdvertisementObserver = void (*)(
    const ShotStopperBleAdvertisement &, void *);
using ShotStopperBleScanCoordinator = bool (*)(bool, uint32_t, void *);

inline uint32_t testScaleReservations = 0;
inline uint32_t testAdvertisementPublications = 0;
inline bool testScaleLeaseAvailable = true;
inline bool testScaleLeaseActive = false;
inline uint32_t testScaleLeaseId = 0;
inline bool testBleCritical = false;
inline uint32_t testBleCriticalEpoch = 0;
inline ShotStopperBleAdvertisementObserver testMachineObserver = nullptr;
inline void *testMachineObserverContext = nullptr;
inline bool testObservationWindowAvailable = true;
inline bool testMachineProcedureAvailable = true;

inline bool shotStopperBleArbiterRegisterObserver(
    ShotStopperBleOwner owner, ShotStopperBleAdvertisementObserver observer,
    void *context) {
  if (owner != ShotStopperBleOwner::Machine || observer == nullptr ||
      testMachineObserver != nullptr) return false;
  testMachineObserver = observer;
  testMachineObserverContext = context;
  return true;
}
inline bool shotStopperBleArbiterRegisterScanCoordinator(
    ShotStopperBleScanCoordinator, void *) {
  return true;
}
inline bool shotStopperBleArbiterStartObservationWindow(uint32_t durationMs) {
  return durationMs != 0 && testObservationWindowAvailable;
}
inline bool shotStopperBleArbiterPrepareMachineProcedure() {
  return testMachineProcedureAvailable;
}

inline void shotStopperBleArbiterPublishAdvertisement(
    const ShotStopperBleAdvertisement &advertisement) {
  ++testAdvertisementPublications;
  if (testMachineObserver != nullptr) {
    testMachineObserver(advertisement, testMachineObserverContext);
  }
}
inline void shotStopperBleArbiterReserveScaleCandidate() {
  ++testScaleReservations;
}
inline void shotStopperBleArbiterClearScaleReservation() {}
inline bool shotStopperBleArbiterTryAcquire(ShotStopperBleOwner owner,
                                            ShotStopperBleLease &lease) {
  if (!testScaleLeaseAvailable || testScaleLeaseActive) return false;
  testScaleLeaseActive = true;
  lease = {owner, ++testScaleLeaseId, 1};
  return true;
}
inline bool shotStopperBleArbiterLeaseCurrent(
    const ShotStopperBleLease &lease) {
  return testScaleLeaseActive && lease.id == testScaleLeaseId;
}
inline void shotStopperBleArbiterRelease(const ShotStopperBleLease &lease) {
  if (lease.id == testScaleLeaseId) testScaleLeaseActive = false;
}
inline ShotStopperBleArbiterSnapshot shotStopperBleArbiterSnapshot() {
  ShotStopperBleArbiterSnapshot snapshot;
  snapshot.owner = testScaleLeaseActive ? ShotStopperBleOwner::Machine
                                        : ShotStopperBleOwner::None;
  snapshot.critical = testBleCritical;
  snapshot.criticalEpoch = testBleCriticalEpoch;
  return snapshot;
}
