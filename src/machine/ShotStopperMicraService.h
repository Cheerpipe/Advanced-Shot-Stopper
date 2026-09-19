#pragma once

#include "ShotStopperBleArbiter.h"
#include "ShotStopperMachineIntegrationSettings.h"
#include "ShotStopperMachineIntegrationTypes.h"
#include "ShotStopperMicraTiming.h"

#include <LineaMicraBLE.h>
#include <freertos/FreeRTOS.h>

namespace shotstopper {

class ShotStopperMicraService {
 public:
  bool begin();
  void service();
  void publishConfig(const MachineIntegrationPersistedSettings &settings,
                     uint32_t configGeneration);
  bool queue(const MachineIntegrationRequest &request);
  MachineIntegrationStatus status() const;

 private:
  static void observeAdvertisement(
      const ShotStopperBleAdvertisement &advertisement, void *context);
  void acceptAdvertisement(const ShotStopperBleAdvertisement &advertisement);
  void startRequest();
  void handleClientEvent(const lineamicra::ClientEvent &event);

  struct Candidate {
    uint8_t address[6] = {};
    char identity[MICRA_IDENTITY_CAPACITY] = {};
    int8_t rssi = INT8_MIN;
    uint8_t addressType = 0;
    bool used = false;
  };

  enum class Stage : uint8_t {
    IDLE,
    WAIT_CANDIDATES,
    CONNECT,
    AUTH,
    CAPABILITIES,
    BOILERS,
    MODE,
    DISCONNECT
  };

  lineamicra::LineaMicraBLE client_;
  MachineIntegrationPersistedSettings config_ = {};
  MachineIntegrationRequest request_ = {};
  MachineIntegrationStatus status_ = {};
  Candidate candidates_[4] = {};
  uint32_t configGeneration_ = 0;
  uint32_t requestStartedAtMs_ = 0;
  Stage stage_ = Stage::IDLE;
  bool requestPending_ = false;
  bool collectCandidates_ = false;
  bool configChanged_ = false;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

}  // namespace shotstopper
