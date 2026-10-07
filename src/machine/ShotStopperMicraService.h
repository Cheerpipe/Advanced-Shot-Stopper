#pragma once

#include "ShotStopperLineaMicraSettings.h"
#include "ShotStopperLineaMicraTypes.h"
#include "ShotStopperMachineIntegration.h"
#include "ShotStopperMicraPowerState.h"
#include "ShotStopperMicraObservation.h"
#include "ShotStopperMicraWebSocket.h"
#include "ShotStopperMicraTiming.h"
#include "ShotStopperPsram.h"
#include "ShotStopperBootHeap.h"
#include "ShotStopperTaskMutex.h"
#include "ShotStopperOutboundAdmission.h"

#include <atomic>
#include <Arduino.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace shotstopper {

class ShotStopperMicraService {
 public:
  bool begin();
  void inhibitCloud() {
    const bool shot = outboundShotActive.load(std::memory_order_acquire);
    const bool pause = !shot || outboundScaleConnected.load(std::memory_order_acquire);
    if (shot) {
      shotGeneration_.fetch_add(1, std::memory_order_acq_rel);
      if (pause) disconnectGeneration_.fetch_add(1, std::memory_order_acq_rel);
    }
    if (pause || powerActive_.load() || temperatureActive_.load())
      abortRequested_.store(true, std::memory_order_release);
    if (task_ != nullptr) xTaskNotifyGive(task_);
  }
  void publishConfig(const LineaMicraPersistedSettings &settings,
                     uint32_t configGeneration);
  void publishNetworkState(bool staConnected, bool apActive, bool shotActive,
                           bool scaleConnecting);
  bool queueConnect(uint32_t requestId, const char *username,
                    const char *password);
  bool queue(const LineaMicraRequest &request);
  bool selectDiscoveredMachine(const char *serial,
                               LineaMicraPersistedSettings &settings);
  void clearDiscovery();
  LineaMicraStatus status() const;
  LineaMicraCloudCall cloudCall() const;
  HeapLifecycleAggregate heapTelemetry() const;
  MicraWebSocketStatus websocketStatus(bool includeTraffic = true) const;
  LineaMicraDiscoverySnapshot discovery() const;
  MachinePhysicalStartDisposition physicalStart(MachineBackflushPermit *permit = nullptr);
  MachineBackflushSnapshot backflush(bool consume = false);
  void finishBackflush(uint32_t attempt);
  bool cloudFirstQuerySettled() const {
    return cloudFirstQuerySettled_.load(std::memory_order_acquire);
  }

 private:
#if defined(SHOT_STOPPER_HOST_TEST)
  friend struct MicraCancellationTest;
  friend struct MicraWebSocketTest;
#endif
  struct IoBuffer;
  struct WorkBuffer;
  struct WebSocketBuffer;
  struct RequestStateGuard;
  struct PendingRequest {
    LineaMicraRequest request = {};
    LineaMicraPersistedSettings credentials = {};
    uint32_t identityGeneration = 0;
    MicraObservationStamp snapshotStamp;
    bool initialSnapshot = false;
    bool present = false;
  };
  struct DesiredTemperature {
    LineaMicraRequest request = {};
    uint32_t machineConfigGeneration = 0;
    uint32_t retryAtMs = 0;
    bool present = false;
    bool commandAccepted = false;
  };
  struct DesiredPower {
    LineaMicraRequest request = {};
    uint32_t machineConfigGeneration = 0;
    uint32_t shotGeneration = 0;
    uint32_t retryAtMs = 0;
    bool present = false;
    bool commandAccepted = false;
  };

  static void taskEntry(void *context);
  static esp_err_t httpEvent(esp_http_client_event_t *event);
  void taskLoop();
  static void websocketEvent(void *context, const char *, int32_t event, void *data);
  static bool stompFrame(void *context, const struct MicraStompFrame &frame);
  bool websocketAdmitted(bool starting = false) const;
  bool shotTransportPaused() const {
    return outboundScaleConnected.load(std::memory_order_acquire) &&
        (outboundShotActive.load(std::memory_order_acquire) ||
         shotActive_.load(std::memory_order_acquire));
  }
  void serviceWebSocket();
  void stopWebSocket(bool release = false);
  void waitRetry(uint32_t delayMs);
  void execute(PendingRequest &pending);
  bool executeConnect(PendingRequest &pending);
  bool executeObservation(PendingRequest &pending);
  bool executeTemperatureApplication(const LineaMicraRequest &request,
                                     uint32_t machineConfigGeneration);
  bool executePowerApplication(const LineaMicraRequest &request,
                               uint32_t machineConfigGeneration);
  bool ensureSession(LineaMicraPersistedSettings &settings, bool registerKey,
                     bool *renewed = nullptr);
  bool generateInstallationKey(LineaMicraPersistedSettings &settings);
  bool registerInstallation(const LineaMicraPersistedSettings &settings);
  bool signIn(const LineaMicraPersistedSettings &settings);
  bool refreshToken(const LineaMicraPersistedSettings &settings);
  bool listMachines(const LineaMicraPersistedSettings &settings,
                    LineaMicraDiscoverySnapshot &result);
  bool readDashboard(const LineaMicraPersistedSettings &settings,
                     LineaMicraStatus &result,
                     const MicraObservationStamp *snapshot = nullptr);
  bool writeTemperature(const LineaMicraPersistedSettings &settings,
                        uint16_t targetDeciC);
  bool writeStandby(const LineaMicraPersistedSettings &settings);
  bool writePowerOn(const LineaMicraPersistedSettings &settings);
  bool request(const LineaMicraPersistedSettings &settings, const char *url,
               esp_http_client_method_t method, const char *body,
               bool authenticated, const char *purpose, const char *endpoint,
               bool installationInit = false);
  LineaMicraError classifyFailure() const;
  bool applySignedHeaders(const LineaMicraPersistedSettings &settings,
                          char *headers = nullptr, size_t capacity = 0);
  void clearRequestState();
  bool networkEligible(LineaMicraError &error, bool observation = false) const;
  void qualifyBackflushLocked();
  bool backflushReadyLocked() const;
  bool ensureIoBuffer();
  bool ensureWorkBuffer();
  void clearSession();
  void releaseIoBuffer(bool responseValid = true);
  void releaseWorkBuffer();
  void publish(const LineaMicraStatus &status);
  void deferObservation(const PendingRequest &pending,
                        LineaMicraStatus status, LineaMicraError reason);
  void fail(LineaMicraStatus &status, LineaMicraError error);
  void scheduleAutomatic(uint32_t now, bool failed);
  bool temperatureRequestCurrent(const LineaMicraRequest &request,
                                 uint32_t machineConfigGeneration) const;
  bool powerRequestCurrent(const LineaMicraRequest &request,
                           uint32_t machineConfigGeneration) const;
  bool identityCurrent(uint32_t identityGeneration) const;
  bool observationCurrent(uint32_t identityGeneration) const;
  bool temperatureEligible(LineaMicraError &error) const;
  void deferTemperature(const LineaMicraRequest &request,
                        LineaMicraError error, uint32_t delayMs,
                        bool retryable = true);
  void deferPower(const LineaMicraRequest &request,
                  LineaMicraError error, uint32_t delayMs,
                  bool retryable = true);

  mutable TaskMutex mux_;
  LineaMicraPersistedSettings config_ = {};
  LineaMicraPersistedSettings candidate_ = {};
  PendingRequest pending_ = {};
  DesiredTemperature desiredTemperature_ = {};
  DesiredPower desiredPower_ = {};
  LineaMicraStatus published_ = {};
  LineaMicraCloudCall pendingCloudCall_ = {};  // Cloud worker only.
  LineaMicraCloudCall publishedCloudCall_ = {};  // Protected by mux_.
  HeapLifecycleTracker tlsHeap_ = {};
  LineaMicraDiscoverySnapshot discovery_ = {};
  uint32_t configGeneration_ = 0;
  uint32_t identityGeneration_ = 0;
  uint32_t nextAutomaticRequestId_ = 0x80000000UL;
  micra_timing::ObservationSchedule observationSchedule_;
  LineaMicraPowerStateTracker powerState_;
  MicraObservationFence observationFence_;
  MachineBackflushObservations backflush_;
  std::atomic<uint32_t> tokenIssuedAtMs_{0};
  std::atomic<bool> tokenAvailable_{false};
  MicraObservationStamp snapshotStamp_;  // Protected by mux_, captured at subscription.
  bool snapshotPending_ = false;
  bool active_ = false;
  TaskHandle_t task_ = nullptr;
  IoBuffer *io_ = nullptr;
  WorkBuffer *work_ = nullptr;
  WebSocketBuffer *websocket_ = nullptr;
  MicraWebSocketStatus websocketStatus_;
  bool websocketUnexpectedReconnect_ = false;  // Protected by mux_; survives pauses.
  uint8_t websocketAuthRecoveries_ = 0;  // Cloud owner; survives workspace parking.
  std::atomic<bool> websocketRetryRequested_{false};
  std::atomic<bool> staConnected_{false};
  std::atomic<bool> apActive_{false};
  std::atomic<bool> shotActive_{false};
  std::atomic<bool> scaleConnecting_{false};
  std::atomic<bool> observationActive_{false};
  std::atomic<bool> temperatureActive_{false};
  std::atomic<bool> powerActive_{false};
  std::atomic<bool> abortRequested_{false};
  std::atomic<uint32_t> disconnectGeneration_{0};
  std::atomic<uint32_t> shotGeneration_{0};
  std::atomic<bool> clearSessionRequested_{false};
  // Latched after the first cloud request reaches any terminal outcome;
  // consumed by the network boot heap shaper release gate.
  std::atomic<bool> cloudFirstQuerySettled_{false};
  bool wasNetworkReady_ = false;
};

}  // namespace shotstopper
