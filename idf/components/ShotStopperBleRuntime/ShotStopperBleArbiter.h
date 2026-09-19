#pragma once

#include <stddef.h>
#include <stdint.h>

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

using ShotStopperBleAdvertisementObserver = void (*)(
    const ShotStopperBleAdvertisement &advertisement, void *context);
using ShotStopperBleScanCoordinator = bool (*)(bool prepareMachineProcedure,
                                               uint32_t durationMs,
                                               void *context);

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

// Observers are fixed by owner and become immutable when the BLE runtime starts.
bool shotStopperBleArbiterRegisterObserver(
    ShotStopperBleOwner owner, ShotStopperBleAdvertisementObserver observer,
    void *context);
bool shotStopperBleArbiterRegisterScanCoordinator(
    ShotStopperBleScanCoordinator coordinator, void *context);
void shotStopperBleArbiterSealObservers();
void shotStopperBleArbiterPublishAdvertisement(
    const ShotStopperBleAdvertisement &advertisement);
bool shotStopperBleArbiterStartObservationWindow(uint32_t durationMs);
bool shotStopperBleArbiterPrepareMachineProcedure();

// A scale candidate reserves the next peer procedure immediately from the host
// callback. Existing machine work is invalidated and must release its lease.
void shotStopperBleArbiterReserveScaleCandidate();
void shotStopperBleArbiterClearScaleReservation();
bool shotStopperBleArbiterTryAcquire(ShotStopperBleOwner owner,
                                    ShotStopperBleLease &lease);
bool shotStopperBleArbiterLeaseCurrent(const ShotStopperBleLease &lease);
void shotStopperBleArbiterRelease(const ShotStopperBleLease &lease);

// Critical publication is one-way and nonblocking. A rising edge invalidates
// machine work but never denies the higher-priority scale owner.
void shotStopperBleArbiterSetCritical(bool critical);
ShotStopperBleArbiterSnapshot shotStopperBleArbiterSnapshot();

// Runtime reset/stop invalidates every callback epoch and clears live claims.
void shotStopperBleArbiterInvalidateAll();
void shotStopperBleArbiterResetForTest();
