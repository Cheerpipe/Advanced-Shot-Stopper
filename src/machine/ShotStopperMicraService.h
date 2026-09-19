#pragma once

#include "ShotStopperBleArbiter.h"
#include "ShotStopperLineaMicraSettings.h"
#include "ShotStopperLineaMicraTypes.h"
#include "ShotStopperMicraTiming.h"

#include <LineaMicraBLE.h>
#if defined(LINEA_MICRA_BLE_HOST_TEST)
#include "nimble_client_platform.h"
#else
#include <freertos/FreeRTOS.h>
#endif

namespace shotstopper {

class ShotStopperMicraService {
 public:
  bool begin();
  void service();
  void publishConfig(const LineaMicraPersistedSettings &settings,
                     uint32_t configGeneration);
  bool queue(const LineaMicraRequest &request);
  LineaMicraStatus status() const;
  bool takeBinding(LineaMicraBindingResult &result);

 private:
  static void observeAdvertisement(
      const ShotStopperBleAdvertisement &advertisement, void *context);
  void acceptAdvertisement(const ShotStopperBleAdvertisement &advertisement);
  void startRequest();
  void handleClientEvent(const lineamicra::ClientEvent &event);
  void handleSubmission(bool accepted);
  void failRequest();
  void finishRequest();
  void scheduleAutomaticObservation(uint32_t now);
  void publishStatus();

  struct Candidate {
    uint8_t address[6] = {};
    char identity[LINEA_MICRA_IDENTITY_CAPACITY] = {};
    int8_t rssi = INT8_MIN;
    uint8_t addressType = 0;
    bool connectable = false;
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
  // Worker-owned state; producers only touch the pending/published fields below.
  LineaMicraPersistedSettings config_ = {};
  LineaMicraRequest request_ = {};
  LineaMicraStatus workingStatus_ = {};
  LineaMicraPersistedSettings pendingConfig_ = {};
  LineaMicraRequest pendingRequest_ = {};
  LineaMicraStatus publishedStatus_ = {};
  Candidate candidates_[4] = {};
  uint32_t configGeneration_ = 0;
  uint32_t pendingConfigGeneration_ = 0;
  uint32_t acceptedConfigGeneration_ = 0;
  uint32_t requestStartedAtMs_ = 0;
  uint32_t discoveryStartedAtMs_ = 0;
  uint32_t bindingRequestId_ = 0;
  uint32_t bindingConfigGeneration_ = 0;
  uint32_t nextObservationAtMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint32_t nextAutomaticRequestId_ = 0x80000000UL;
  Stage stage_ = Stage::IDLE;
  uint8_t selectedCandidate_ = UINT8_MAX;
  uint8_t attempt_ = 0;
  bool configPending_ = false;
  bool requestPending_ = false;
  bool collectCandidates_ = false;
  bool retryPending_ = false;
  bool automaticRequest_ = false;
  bool bindingReady_ = false;
  bool criticalSeen_ = false;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

static_assert(sizeof(ShotStopperMicraService) <= 2048,
              "Micra service exceeds its fixed internal-RAM envelope");

}  // namespace shotstopper
