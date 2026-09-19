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

inline uint32_t testScaleReservations = 0;
inline uint32_t testAdvertisementPublications = 0;
inline bool testScaleLeaseAvailable = true;
inline bool testScaleLeaseActive = false;
inline uint32_t testScaleLeaseId = 0;

inline void shotStopperBleArbiterPublishAdvertisement(
    const ShotStopperBleAdvertisement &) {
  ++testAdvertisementPublications;
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
