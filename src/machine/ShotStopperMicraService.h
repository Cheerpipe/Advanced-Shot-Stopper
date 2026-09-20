#pragma once

#include "ShotStopperLineaMicraSettings.h"
#include "ShotStopperLineaMicraTypes.h"
#include "ShotStopperMachineIntegration.h"
#include "ShotStopperMicraPowerState.h"
#include "ShotStopperTaskMutex.h"

#include <atomic>
#include <Arduino.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace shotstopper {

class ShotStopperMicraService {
 public:
  bool begin();
  void publishConfig(const LineaMicraPersistedSettings &settings,
                     uint32_t configGeneration);
  void publishNetworkState(bool staConnected, bool apActive, bool shotActive);
  bool queueConnect(uint32_t requestId, const char *username,
                    const char *password);
  bool queue(const LineaMicraRequest &request);
  bool selectDiscoveredMachine(const char *serial,
                               LineaMicraPersistedSettings &settings);
  void clearDiscovery();
  void serviceAbort();
  LineaMicraStatus status() const;
  LineaMicraDiscoverySnapshot discovery() const;
  MachinePhysicalStartDisposition physicalStart();

 private:
  struct IoBuffer;
  struct WorkBuffer;
  struct RequestStateGuard;
  struct PendingRequest {
    LineaMicraRequest request = {};
    LineaMicraPersistedSettings credentials = {};
    bool present = false;
  };

  static void taskEntry(void *context);
  static esp_err_t httpEvent(esp_http_client_event_t *event);
  void taskLoop();
  void execute(PendingRequest &pending);
  bool executeConnect(PendingRequest &pending);
  bool executeObservation(PendingRequest &pending);
  bool ensureSession(LineaMicraPersistedSettings &settings, bool registerKey,
                     bool *renewed = nullptr);
  bool generateInstallationKey(LineaMicraPersistedSettings &settings);
  bool registerInstallation(const LineaMicraPersistedSettings &settings);
  bool signIn(const LineaMicraPersistedSettings &settings);
  bool refreshToken(const LineaMicraPersistedSettings &settings);
  bool listMachines(const LineaMicraPersistedSettings &settings,
                    LineaMicraDiscoverySnapshot &result);
  bool readDashboard(const LineaMicraPersistedSettings &settings,
                     LineaMicraStatus &result);
  bool request(const LineaMicraPersistedSettings &settings, const char *url,
               esp_http_client_method_t method, const char *body,
               bool authenticated,
               bool installationInit = false);
  bool applySignedHeaders(const LineaMicraPersistedSettings &settings);
  void clearRequestState();
  bool networkEligible(LineaMicraError &error) const;
  bool ensureIoBuffer();
  bool ensureWorkBuffer();
  void clearSession();
  void releaseIoBuffer();
  void releaseWorkBuffer();
  void publish(const LineaMicraStatus &status);
  void publishObservation(const LineaMicraStatus &status,
                          uint32_t powerGeneration);
  void fail(LineaMicraStatus &status, LineaMicraError error);
  void scheduleAutomatic(uint32_t now, bool failed);

  mutable TaskMutex mux_;
  TaskMutex clientMux_;
  LineaMicraPersistedSettings config_ = {};
  LineaMicraPersistedSettings candidate_ = {};
  PendingRequest pending_ = {};
  LineaMicraStatus published_ = {};
  LineaMicraDiscoverySnapshot discovery_ = {};
  uint32_t configGeneration_ = 0;
  uint32_t nextAutomaticRequestId_ = 0x80000000UL;
  uint32_t nextAutomaticAtMs_ = 0;
  LineaMicraPowerStateTracker powerState_;
  bool active_ = false;
  TaskHandle_t task_ = nullptr;
  IoBuffer *io_ = nullptr;
  WorkBuffer *work_ = nullptr;
  std::atomic<bool> staConnected_{false};
  std::atomic<bool> apActive_{false};
  std::atomic<bool> shotActive_{false};
  std::atomic<bool> abortRequested_{false};
  std::atomic<bool> clearSessionRequested_{false};
  esp_http_client_handle_t activeClient_ = nullptr;
};

}  // namespace shotstopper
