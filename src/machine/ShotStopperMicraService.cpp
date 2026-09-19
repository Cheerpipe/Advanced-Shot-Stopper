#include "ShotStopperMicraService.h"
#include "ShotStopperDomain.h"

#include <Arduino.h>
#include <cerrno>
#include <cstring>
#if !defined(LINEA_MICRA_BLE_HOST_TEST)
#include "host/ble_hs.h"
#endif

void serialTraceCategoryf(shotstopper::LogLevel level,
                          shotstopper::DebugCategory category,
                          const char *format, ...);

namespace shotstopper {
namespace {

void incrementSaturating(uint16_t &value) {
  if (value != UINT16_MAX) ++value;
}

uint32_t peerKey(uint8_t type, const uint8_t *address) {
  uint32_t key = 2166136261UL ^ type;
  for (uint8_t index = 0; index < 6; ++index) {
    key = (key ^ address[index]) * 16777619UL;
  }
  return key == 0 ? UINT32_MAX : key;
}

}  // namespace

const char *ShotStopperMicraService::stageName(Stage stage) {
  switch (stage) {
    case Stage::IDLE: return "idle";
    case Stage::WAIT_CANDIDATES: return "discovery";
    case Stage::CONNECT: return "connect";
    case Stage::AUTH: return "auth";
    case Stage::CAPABILITIES: return "capabilities";
    case Stage::BOILERS: return "boilers";
    case Stage::MODE: return "mode";
    case Stage::DISCONNECT: return "disconnect";
  }
  return "unknown";
}

bool ShotStopperMicraService::begin() {
  const bool registered = shotStopperBleArbiterRegisterObserver(
      ShotStopperBleOwner::Machine, observeAdvertisement, this);
  if (!registered) {
    serialTraceCategoryf(LogLevel::WARNING, DebugCategory::SYSTEM,
                         "Micra BLE observer registration failed");
  }
  return registered;
}

void ShotStopperMicraService::service() {
  LineaMicraPersistedSettings nextConfig;
  uint32_t nextConfigGeneration = 0;
  portENTER_CRITICAL(&mux_);
  const bool configChanged = configPending_;
  if (configChanged) {
    nextConfig = pendingConfig_;
    nextConfigGeneration = pendingConfigGeneration_;
    configPending_ = false;
    collectCandidates_ = false;
  }
  portEXIT_CRITICAL(&mux_);
  if (configChanged) {
    const bool sameIntegrationConfig =
        memcmp(&config_, &nextConfig, sizeof(config_)) == 0;
    const bool retainTemperature =
        memcmp(config_.token, nextConfig.token, sizeof(config_.token)) == 0 &&
        workingStatus_.measuredValid && workingStatus_.targetValid;
    const LineaMicraStatus previousStatus = workingStatus_;
    client_.abort();
    config_ = nextConfig;
    configGeneration_ = nextConfigGeneration;
    request_ = {};
    workingStatus_ = sameIntegrationConfig ? previousStatus
                                           : LineaMicraStatus{};
    if (!sameIntegrationConfig && retainTemperature) {
      workingStatus_.measuredDeciC = previousStatus.measuredDeciC;
      workingStatus_.targetDeciC = previousStatus.targetDeciC;
      workingStatus_.temperatureAtMs = previousStatus.temperatureAtMs;
      workingStatus_.measuredValid = workingStatus_.targetValid = true;
    }
    workingStatus_.configGeneration = configGeneration_;
    workingStatus_.requestId = 0;
    const bool configured = strnlen(config_.token, sizeof(config_.token)) == 64;
    workingStatus_.phase = configured ? LineaMicraPhase::IDLE
                                      : LineaMicraPhase::Disabled;
    if (!sameIntegrationConfig) {
      workingStatus_.quality = configured
                                 ? ((config_.options & LINEA_MICRA_OBSERVE_STATE) != 0
                                        ? LineaMicraObservationQuality::UNCONFIGURED
                                        : LineaMicraObservationQuality::Disabled)
                                 : LineaMicraObservationQuality::Disabled;
    }
    stage_ = Stage::IDLE;
    retryPending_ = false;
    automaticRequest_ = false;
    bindingReady_ = false;
    bindingRequestId_ = bindingConfigGeneration_ = 0;
    selectedCandidate_ = UINT8_MAX;
    attempt_ = 0;
    nextObservationAtMs_ = retryAtMs_ = millis();
    publishStatus();
  }
  client_.service();
  lineamicra::ClientEvent event;
  if (client_.takeEvent(event)) handleClientEvent(event);
  if (stage_ == Stage::DISCONNECT &&
      client_.state() == lineamicra::ClientState::IDLE) {
    stage_ = Stage::IDLE;
  }

  const ShotStopperBleArbiterSnapshot arbiter =
      shotStopperBleArbiterSnapshot();
  if (arbiter.critical) {
    criticalSeen_ = true;
  } else if (criticalSeen_) {
    criticalSeen_ = false;
    const uint32_t now = millis();
    if (workingStatus_.phase != LineaMicraPhase::FAILED ||
        static_cast<int32_t>(now - nextObservationAtMs_) >= 0) {
      nextObservationAtMs_ = now;
    }
  }

  bool start = false;
  bool retryStart = false;
  if (stage_ == Stage::IDLE &&
      client_.state() == lineamicra::ClientState::IDLE) {
    const uint32_t now = millis();
    if (retryPending_ && static_cast<int32_t>(now - retryAtMs_) >= 0) {
      retryPending_ = false;
      start = true;
      retryStart = true;
    } else if (!retryPending_) {
      portENTER_CRITICAL(&mux_);
      const bool observeCooldown =
          requestPending_ &&
          pendingRequest_.type == LineaMicraRequestType::OBSERVE_STATE &&
          publishedStatus_.phase == LineaMicraPhase::FAILED &&
          static_cast<int32_t>(now - nextObservationAtMs_) < 0;
      if (requestPending_ && !observeCooldown &&
          static_cast<int32_t>(now - retryAtMs_) >= 0) {
        request_ = pendingRequest_;
        requestPending_ = false;
        start = true;
      }
      portEXIT_CRITICAL(&mux_);
      if (!start) scheduleAutomaticObservation(now);
      if (request_.requestId != 0 && automaticRequest_) start = true;
    }
  }
  if (start) {
    workingStatus_.requestId = request_.requestId;
    workingStatus_.configGeneration = request_.configGeneration;
    if (request_.type == LineaMicraRequestType::OBSERVE_STATE) {
      workingStatus_.activityGeneration = request_.activityGeneration;
    }
    workingStatus_.phase = LineaMicraPhase::QUEUED;
    if (request_.type != LineaMicraRequestType::OBSERVE_STATE) {
      workingStatus_.measuredValid = false;
      workingStatus_.targetValid = false;
    }
    if (!retryStart) {
      attempt_ = 0;
      requestStartedAtMs_ = millis();
    }
    const bool collect = request_.type == LineaMicraRequestType::TEST &&
                         !config_.bindingVerified;
    portENTER_CRITICAL(&mux_);
    collectCandidates_ = collect;
    if (collect) {
      memset(candidates_, 0, sizeof(candidates_));
      selectedCandidate_ = UINT8_MAX;
      advertisementsObserved_ = connectableFragments_ = namedFragments_ = 0;
      micraFragments_ = malformedFragments_ = 0;
    }
    portEXIT_CRITICAL(&mux_);
    stage_ = collect ? Stage::WAIT_CANDIDATES : Stage::CONNECT;
    if (collect) discoveryStartedAtMs_ = millis();
    start = !collect;
    publishStatus();
    if (collect && !shotStopperBleArbiterStartObservationWindow(
                       micra_timing::kDiscoverySliceMs)) {
      const ShotStopperBleArbiterSnapshot state =
          shotStopperBleArbiterSnapshot();
      serialTraceCategoryf(
          LogLevel::WARNING, DebugCategory::SYSTEM,
          "Micra scan denied req=%lu owner=%u critical=%u reserved=%u",
          static_cast<unsigned long>(request_.requestId),
          static_cast<unsigned>(state.owner),
          static_cast<unsigned>(state.critical),
          static_cast<unsigned>(state.scaleReserved));
      failRequest(-EBUSY);
      return;
    }
    if (collect) {
      serialTraceCategoryf(
          LogLevel::DEBUG, DebugCategory::SYSTEM,
          "Micra scan started req=%lu windowMs=%lu",
          static_cast<unsigned long>(request_.requestId),
          static_cast<unsigned long>(micra_timing::kDiscoverySliceMs));
    }
  }
  if (start) startRequest();
  if (stage_ == Stage::WAIT_CANDIDATES &&
      static_cast<uint32_t>(millis() - discoveryStartedAtMs_) >=
          micra_timing::kDiscoverySliceMs) {
    uint8_t eligible = 0, fallback = 0;
    uint16_t seen = 0, connectable = 0, named = 0, micra = 0, malformed = 0;
    portENTER_CRITICAL(&mux_);
    collectCandidates_ = false;
    for (const Candidate &candidate : candidates_) {
      eligible += candidate.used && candidate.connectable &&
                  strncmp(candidate.identity, "MICRA_", 6) == 0;
      fallback += candidate.used && candidate.connectable &&
                  candidate.identity[0] == '\0';
    }
    seen = advertisementsObserved_;
    connectable = connectableFragments_;
    named = namedFragments_;
    micra = micraFragments_;
    malformed = malformedFragments_;
    portEXIT_CRITICAL(&mux_);
    serialTraceCategoryf(
        LogLevel::DEBUG, DebugCategory::SYSTEM,
        "Micra scan req=%lu seen=%u conn=%u named=%u match=%u bad=%u eligible=%u fallback=%u",
        static_cast<unsigned long>(request_.requestId),
        static_cast<unsigned>(seen), static_cast<unsigned>(connectable),
        static_cast<unsigned>(named), static_cast<unsigned>(micra),
        static_cast<unsigned>(malformed), static_cast<unsigned>(eligible),
        static_cast<unsigned>(fallback));
    startRequest();
  }
}

void ShotStopperMicraService::scheduleAutomaticObservation(uint32_t now) {
  const bool enabled = (config_.options & LINEA_MICRA_OBSERVE_STATE) != 0;
  const ShotStopperBleArbiterSnapshot arbiter =
      shotStopperBleArbiterSnapshot();
  if (!enabled || !config_.bindingVerified || config_.token[0] == '\0' ||
      static_cast<int32_t>(now - nextObservationAtMs_) < 0 ||
      static_cast<int32_t>(now - retryAtMs_) < 0 || arbiter.critical) {
    return;
  }
  if (++nextAutomaticRequestId_ == 0) nextAutomaticRequestId_ = 0x80000000UL;
  request_ = {};
  request_.requestId = nextAutomaticRequestId_;
  request_.configGeneration = configGeneration_;
  request_.activityGeneration = arbiter.criticalEpoch;
  request_.type = LineaMicraRequestType::OBSERVE_STATE;
  automaticRequest_ = true;
  attempt_ = 0;
  requestStartedAtMs_ = now;
}

void ShotStopperMicraService::observeAdvertisement(
    const ShotStopperBleAdvertisement &advertisement, void *context) {
  static_cast<ShotStopperMicraService *>(context)->acceptAdvertisement(
      advertisement);
}

void ShotStopperMicraService::publishConfig(
    const LineaMicraPersistedSettings &settings,
    uint32_t configGeneration) {
  portENTER_CRITICAL(&mux_);
  pendingConfig_ = settings;
  pendingConfigGeneration_ = configGeneration;
  acceptedConfigGeneration_ = configGeneration;
  configPending_ = true;
  requestPending_ = false;
  collectCandidates_ = false;
  bindingReady_ = false;
  memset(candidates_, 0, sizeof(candidates_));
  memset(rejectedPeerKeys_, 0, sizeof(rejectedPeerKeys_));
  portEXIT_CRITICAL(&mux_);
}

void ShotStopperMicraService::startRequest() {
  portENTER_CRITICAL(&mux_);
  const bool configSuperseded = configPending_;
  portEXIT_CRITICAL(&mux_);
  if (configSuperseded) return;
  lineamicra::PeerAddress peer;
  if (config_.bindingVerified) {
    peer.type = config_.peerAddressType;
    memcpy(peer.value, config_.peerAddress, sizeof(peer.value));
  } else {
    Candidate best = {};
    bool found = false;
    portENTER_CRITICAL(&mux_);
    uint8_t index = 0;
    uint8_t bestIndex = UINT8_MAX;
    uint8_t anonymousCount = 0;
    for (const Candidate &candidate : candidates_) {
      if (candidate.used && candidate.connectable &&
          strncmp(candidate.identity, "MICRA_", 6) == 0 &&
          (!found || candidate.rssi > best.rssi ||
           (candidate.rssi == best.rssi &&
            memcmp(candidate.address, best.address,
                   sizeof(candidate.address)) < 0))) {
        best = candidate;
        found = true;
        bestIndex = index;
      }
      anonymousCount += candidate.used && candidate.connectable &&
                        candidate.identity[0] == '\0';
      ++index;
    }
    if (!found && anonymousCount != 0) {
      const uint8_t wantedRank = attempt_ % anonymousCount;
      index = 0;
      for (const Candidate &candidate : candidates_) {
        if (!candidate.used || !candidate.connectable ||
            candidate.identity[0] != '\0') {
          ++index;
          continue;
        }
        uint8_t rank = 0;
        for (const Candidate &other : candidates_) {
          rank += other.used && other.connectable &&
                  other.identity[0] == '\0' &&
                  (other.rssi > candidate.rssi ||
                   (other.rssi == candidate.rssi &&
                    memcmp(other.address, candidate.address,
                           sizeof(candidate.address)) < 0));
        }
        if (rank == wantedRank) {
          best = candidate;
          bestIndex = index;
          found = true;
          break;
        }
        ++index;
      }
    }
    portEXIT_CRITICAL(&mux_);
    if (!found) {
      failRequest(-ENOENT);
      return;
    }
    peer.type = best.addressType;
    memcpy(peer.value, best.address, sizeof(peer.value));
    selectedCandidate_ = bestIndex;
    serialTraceCategoryf(
        LogLevel::DEBUG, DebugCategory::SYSTEM,
        "Micra candidate req=%lu id=%s addr=%02X:%02X:%02X:%02X:%02X:%02X rssi=%d type=%u",
        static_cast<unsigned long>(request_.requestId),
        best.identity[0] == '\0' ? "anonymous" : best.identity,
        best.address[5], best.address[4], best.address[3], best.address[2],
        best.address[1], best.address[0], best.rssi,
        static_cast<unsigned>(best.addressType));
  }
  if (!shotStopperBleArbiterPrepareMachineProcedure()) {
    failRequest(-EBUSY);
    return;
  }
  workingStatus_.phase = LineaMicraPhase::RUNNING;
  publishStatus();
  stage_ = Stage::CONNECT;
  serialTraceCategoryf(
      LogLevel::DEBUG, DebugCategory::SYSTEM,
      "Micra connect req=%lu attempt=%u bound=%u type=%u",
      static_cast<unsigned long>(request_.requestId), attempt_ + 1U,
      static_cast<unsigned>(config_.bindingVerified),
      static_cast<unsigned>(peer.type));
  handleSubmission(client_.connect(peer,
                                   {micra_timing::kConnectTimeoutMs,
                                    micra_timing::kAttTimeoutMs,
                                    micra_timing::kConnectedSessionMaxMs},
                                   request_.requestId));
}

void ShotStopperMicraService::handleClientEvent(
    const lineamicra::ClientEvent &event) {
  if (event.requestGeneration != request_.requestId ||
      request_.configGeneration != configGeneration_) {
    return;
  }
  if (event.type == lineamicra::EventType::DISCONNECTED) {
    if (stage_ != Stage::DISCONNECT &&
        workingStatus_.phase != LineaMicraPhase::CONFIRMED) {
      failRequest(event.status);
      return;
    }
    stage_ = Stage::IDLE;
    return;
  }
  if (event.type == lineamicra::EventType::ERROR) {
    bool added = false;
    uint8_t cacheCount = 0, address[6] = {};
    if (event.status == BLE_HS_ENOENT && stage_ == Stage::CONNECT &&
        !config_.bindingVerified && selectedCandidate_ < 4) {
      portENTER_CRITICAL(&mux_);
      const Candidate &candidate = candidates_[selectedCandidate_];
      if (candidate.used && candidate.identity[0] == '\0') {
        const uint32_t key = peerKey(candidate.addressType, candidate.address);
        bool known = false;
        for (uint32_t rejectedKey : rejectedPeerKeys_) {
          known |= rejectedKey == key;
        }
        for (uint32_t &rejectedKey : rejectedPeerKeys_) {
          if (!known && rejectedKey == 0) {
            rejectedKey = key;
            added = true;
            known = true;
          }
          cacheCount += rejectedKey != 0;
        }
        memcpy(address, candidate.address, sizeof(address));
      }
      portEXIT_CRITICAL(&mux_);
    }
    if (added) {
      serialTraceCategoryf(
          LogLevel::DEBUG, DebugCategory::SYSTEM,
          "Micra reject req=%lu addr=%02X:%02X:%02X:%02X:%02X:%02X cache=%u",
          static_cast<unsigned long>(request_.requestId), address[5], address[4],
          address[3], address[2], address[1], address[0],
          static_cast<unsigned>(cacheCount));
    }
    failRequest(event.status);
    return;
  }
  if (event.type == lineamicra::EventType::READY) {
    stage_ = Stage::AUTH;
    handleSubmission(client_.authenticate(config_.token, 64,
                                           request_.requestId));
    return;
  }
  if (event.type == lineamicra::EventType::AUTHENTICATED) {
    if (request_.type == LineaMicraRequestType::OBSERVE_STATE) {
      stage_ = Stage::MODE;
      handleSubmission(client_.query(lineamicra::Query::MACHINE_MODE,
                                     request_.requestId));
    } else if (!config_.bindingVerified) {
      stage_ = Stage::CAPABILITIES;
      handleSubmission(client_.query(lineamicra::Query::MACHINE_CAPABILITIES,
                                     request_.requestId));
    } else {
      stage_ = Stage::BOILERS;
      handleSubmission(client_.query(lineamicra::Query::BOILERS,
                                     request_.requestId));
    }
    return;
  }
  if (event.type != lineamicra::EventType::RESPONSE) return;
  lineamicra::Error error = lineamicra::Error::NONE;
  if (stage_ == Stage::CAPABILITIES) {
    if (lineamicra::parseMachineCapabilities(event.payload, event.payloadLength,
                                              error)) {
      if (!config_.bindingVerified && selectedCandidate_ < 4) {
        portENTER_CRITICAL(&mux_);
        Candidate &candidate = candidates_[selectedCandidate_];
        if (candidate.used && candidate.identity[0] == '\0') {
          memcpy(candidate.identity, "MICRA_ANONYMOUS",
                 sizeof("MICRA_ANONYMOUS"));
        }
        portEXIT_CRITICAL(&mux_);
      }
      stage_ = Stage::BOILERS;
      handleSubmission(client_.query(lineamicra::Query::BOILERS,
                                     request_.requestId));
      return;
    }
  } else if (stage_ == Stage::BOILERS) {
    lineamicra::BrewBoiler boiler;
    if (lineamicra::parseBrewBoiler(event.payload, event.payloadLength, boiler,
                                    error)) {
      workingStatus_.measuredDeciC = boiler.currentDeciC;
      workingStatus_.targetDeciC = boiler.targetDeciC;
      workingStatus_.measuredValid = workingStatus_.targetValid = true;
      workingStatus_.temperatureAtMs = millis();
      workingStatus_.phase = LineaMicraPhase::CONFIRMED;
      publishStatus();
      if (request_.type == LineaMicraRequestType::TEST) {
        serialTraceCategoryf(
            LogLevel::INFO, DebugCategory::SYSTEM,
            "Micra test confirmed req=%lu measuredDeciC=%u targetDeciC=%u",
            static_cast<unsigned long>(request_.requestId),
            static_cast<unsigned>(boiler.currentDeciC),
            static_cast<unsigned>(boiler.targetDeciC));
      }
      if (request_.type == LineaMicraRequestType::TEST &&
          !config_.bindingVerified && selectedCandidate_ < 4) {
        portENTER_CRITICAL(&mux_);
        bindingRequestId_ = request_.requestId;
        bindingConfigGeneration_ = request_.configGeneration;
        bindingReady_ = true;
        portEXIT_CRITICAL(&mux_);
      }
      finishRequest();
      client_.disconnect();
      stage_ = Stage::DISCONNECT;
      return;
    }
  } else if (stage_ == Stage::MODE) {
    lineamicra::Mode mode;
    if (lineamicra::parseMachineMode(event.payload, event.payloadLength, mode,
                                     error)) {
      workingStatus_.sampleAtMs = millis();
      workingStatus_.observedMode =
          mode == lineamicra::Mode::STANDBY
              ? LineaMicraObservedMode::STANDBY
              : (mode == lineamicra::Mode::BREWING
                     ? LineaMicraObservedMode::BREWING
                     : (mode == lineamicra::Mode::ECO
                            ? LineaMicraObservedMode::ECO
                            : LineaMicraObservedMode::UNSUPPORTED));
      workingStatus_.powerState = mode == lineamicra::Mode::STANDBY
                               ? LineaMicraPowerState::OFF
                               : (mode == lineamicra::Mode::BREWING
                                      ? LineaMicraPowerState::ON
                                      : LineaMicraPowerState::UNKNOWN);
      workingStatus_.effectiveOn =
          workingStatus_.powerState != LineaMicraPowerState::OFF;
      workingStatus_.quality =
          workingStatus_.powerState == LineaMicraPowerState::UNKNOWN
              ? LineaMicraObservationQuality::UNSUPPORTED
              : LineaMicraObservationQuality::CURRENT;
      workingStatus_.phase = LineaMicraPhase::CONFIRMED;
      publishStatus();
      finishRequest();
      client_.disconnect();
      stage_ = Stage::DISCONNECT;
      return;
    }
  }
  serialTraceCategoryf(
      LogLevel::WARNING, DebugCategory::SYSTEM,
      "Micra response rejected req=%lu stage=%s parser=%u length=%u",
      static_cast<unsigned long>(request_.requestId), stageName(stage_),
      static_cast<unsigned>(error),
      static_cast<unsigned>(event.payloadLength));
  failRequest(-static_cast<int32_t>(error));
}

void ShotStopperMicraService::handleSubmission(bool accepted) {
  if (accepted) return;
  lineamicra::ClientEvent event;
  if (client_.takeEvent(event)) handleClientEvent(event);
  else failRequest(-EBUSY);
}

void ShotStopperMicraService::failRequest(int32_t status) {
  const Stage failedStage = stage_;
  const uint32_t requestId = request_.requestId;
  workingStatus_.quality = LineaMicraObservationQuality::COMMUNICATION_ERROR;
  workingStatus_.powerState = LineaMicraPowerState::UNKNOWN;
  workingStatus_.effectiveOn = true;
  const uint32_t now = millis();
  const bool withinDeadline =
      static_cast<uint32_t>(now - requestStartedAtMs_) <
      micra_timing::kRequestDeadlineMs;
  const uint32_t jitter =
      (request_.requestId ^ (static_cast<uint32_t>(attempt_) + 1U) *
                                2654435761UL) %
      (micra_timing::kJitterMaxMs + 1U);
  uint32_t retryDelayMs = 0;
  if (withinDeadline && attempt_ + 1U < micra_timing::kMaxAttempts) {
    retryDelayMs = micra_timing::kRetryDelaysMs[attempt_] + jitter;
    retryAtMs_ = now + retryDelayMs;
    ++attempt_;
    retryPending_ = true;
    workingStatus_.phase = LineaMicraPhase::BACKOFF;
  } else {
    workingStatus_.phase = LineaMicraPhase::FAILED;
    retryPending_ = false;
    nextObservationAtMs_ = now + micra_timing::kExhaustedCooldownMs + jitter;
    retryAtMs_ = now + micra_timing::kMinDisconnectedMs;
  }
  const lineamicra::ClientHealth health = client_.health();
  const ShotStopperBleArbiterSnapshot arbiter = shotStopperBleArbiterSnapshot();
  serialTraceCategoryf(
      LogLevel::WARNING, DebugCategory::SYSTEM,
      "Micra fail req=%lu stage=%s raw=%ld try=%u/%u retryMs=%lu client=%u mtu=%u",
      static_cast<unsigned long>(requestId), stageName(failedStage),
      static_cast<long>(status), attempt_ + (retryPending_ ? 0U : 1U),
      static_cast<unsigned>(micra_timing::kMaxAttempts),
      static_cast<unsigned long>(retryDelayMs),
      static_cast<unsigned>(client_.state()),
      static_cast<unsigned>(health.negotiatedMtu));
  serialTraceCategoryf(
      LogLevel::DEBUG, DebugCategory::SYSTEM,
      "Micra arbiter owner=%u epoch=%lu critical=%u reserved=%u preempt=%lu denials=%lu",
      static_cast<unsigned>(arbiter.owner),
      static_cast<unsigned long>(arbiter.epoch),
      static_cast<unsigned>(arbiter.critical),
      static_cast<unsigned>(arbiter.scaleReserved),
      static_cast<unsigned long>(arbiter.machinePreemptions),
      static_cast<unsigned long>(arbiter.machineDenials));
  if (health.staleCallbacks || health.droppedEvents || health.rejectedResponses ||
      health.mbufFailures) {
    serialTraceCategoryf(
        LogLevel::DEBUG, DebugCategory::SYSTEM,
        "Micra health stale=%lu drops=%lu rejected=%lu mbuf=%lu",
        static_cast<unsigned long>(health.staleCallbacks),
        static_cast<unsigned long>(health.droppedEvents),
        static_cast<unsigned long>(health.rejectedResponses),
        static_cast<unsigned long>(health.mbufFailures));
  }
  if (!retryPending_) {
    request_ = {};
    automaticRequest_ = false;
  }
  publishStatus();
  client_.disconnect();
  stage_ = client_.state() == lineamicra::ClientState::IDLE
               ? Stage::IDLE
               : Stage::DISCONNECT;
}

void ShotStopperMicraService::finishRequest() {
  retryPending_ = false;
  attempt_ = 0;
  nextObservationAtMs_ = millis() + micra_timing::kStatePollNoScaleMs;
  retryAtMs_ = millis() + micra_timing::kMinDisconnectedMs;
  request_ = {};
  automaticRequest_ = false;
}

void ShotStopperMicraService::publishStatus() {
  portENTER_CRITICAL(&mux_);
  publishedStatus_ = workingStatus_;
  portEXIT_CRITICAL(&mux_);
}

bool ShotStopperMicraService::queue(
    const LineaMicraRequest &request) {
  const uint32_t activityGeneration =
      shotStopperBleArbiterSnapshot().criticalEpoch;
  portENTER_CRITICAL(&mux_);
  const LineaMicraPhase phase = publishedStatus_.phase;
  const bool available = phase == LineaMicraPhase::IDLE ||
                         phase == LineaMicraPhase::CONFIRMED ||
                         phase == LineaMicraPhase::FAILED;
  const bool supported = request.type == LineaMicraRequestType::TEST ||
                         (request.type == LineaMicraRequestType::OBSERVE_STATE &&
                          (pendingConfig_.options &
                           LINEA_MICRA_OBSERVE_STATE) != 0);
  const bool accepted = supported && request.requestId != 0 &&
                        request.configGeneration == acceptedConfigGeneration_ &&
                        strnlen(pendingConfig_.token,
                                sizeof(pendingConfig_.token)) == 64 &&
                        available && !bindingReady_ && !requestPending_;
  if (accepted) {
    pendingRequest_ = request;
    if (pendingRequest_.type == LineaMicraRequestType::OBSERVE_STATE) {
      pendingRequest_.activityGeneration = activityGeneration;
    }
    requestPending_ = true;
  }
  portEXIT_CRITICAL(&mux_);
  return accepted;
}

LineaMicraStatus ShotStopperMicraService::status() const {
  portENTER_CRITICAL(&mux_);
  const LineaMicraStatus snapshot = publishedStatus_;
  portEXIT_CRITICAL(&mux_);
  LineaMicraStatus effective = snapshot;
  const ShotStopperBleArbiterSnapshot arbiter =
      shotStopperBleArbiterSnapshot();
  if ((effective.quality == LineaMicraObservationQuality::CURRENT ||
       effective.quality == LineaMicraObservationQuality::UNSUPPORTED) &&
      effective.sampleAtMs != 0 &&
      (effective.activityGeneration != arbiter.criticalEpoch ||
       static_cast<uint32_t>(millis() - effective.sampleAtMs) >=
           micra_timing::kStateFreshnessMs)) {
    effective.quality = LineaMicraObservationQuality::STALE;
    effective.powerState = LineaMicraPowerState::UNKNOWN;
    effective.effectiveOn = true;
  }
  return effective;
}

bool ShotStopperMicraService::takeBinding(
    LineaMicraBindingResult &result) {
  portENTER_CRITICAL(&mux_);
  if (!bindingReady_ || selectedCandidate_ >= 4 ||
      !candidates_[selectedCandidate_].used ||
      !candidates_[selectedCandidate_].connectable ||
      strncmp(candidates_[selectedCandidate_].identity, "MICRA_", 6) != 0) {
    portEXIT_CRITICAL(&mux_);
    return false;
  }
  const Candidate candidate = candidates_[selectedCandidate_];
  result = {};
  result.requestId = bindingRequestId_;
  result.configGeneration = bindingConfigGeneration_;
  result.addressType = candidate.addressType;
  memcpy(result.address, candidate.address, sizeof(result.address));
  memcpy(result.identity, candidate.identity, sizeof(result.identity));
  portEXIT_CRITICAL(&mux_);
  return true;
}

void ShotStopperMicraService::acceptAdvertisement(
    const ShotStopperBleAdvertisement &advertisement) {
  portENTER_CRITICAL(&mux_);
  const bool collecting = collectCandidates_;
  if (collecting) incrementSaturating(advertisementsObserved_);
  portEXIT_CRITICAL(&mux_);
  if (!collecting) return;
  const auto noteMalformed = [this]() {
    portENTER_CRITICAL(&mux_);
    if (collectCandidates_) incrementSaturating(malformedFragments_);
    portEXIT_CRITICAL(&mux_);
  };
  bool nonzeroAddress = false;
  for (uint8_t value : advertisement.address) nonzeroAddress |= value != 0;
  if (!nonzeroAddress || advertisement.addressType > 3) {
    noteMalformed();
    return;
  }
  char identity[LINEA_MICRA_IDENTITY_CAPACITY] = {};
  size_t offset = 0;
  while (advertisement.payload != nullptr &&
         offset < advertisement.payloadLength) {
    const uint8_t fieldLength = advertisement.payload[offset];
    if (fieldLength == 0) break;
    if (offset + fieldLength >= advertisement.payloadLength) {
      noteMalformed();
      return;
    }
    const uint8_t type = advertisement.payload[offset + 1];
    if ((type == 0x08 || type == 0x09) && fieldLength > 1) {
      const size_t length = fieldLength - 1;
      if (length >= sizeof(identity)) {
        noteMalformed();
        return;
      }
      memcpy(identity, advertisement.payload + offset + 2, length);
      identity[length] = '\0';
      break;
    }
    offset += static_cast<size_t>(fieldLength) + 1U;
  }
  const bool hasIdentity = identity[0] != '\0';
  bool printableIdentity = true;
  for (const char value : identity) {
    if (value == '\0') break;
    if (value < 0x20 || value > 0x7e) {
      printableIdentity = false;
      break;
    }
  }
  const bool micraIdentity = hasIdentity && printableIdentity &&
                             strncmp(identity, "MICRA_", 6) == 0;
  portENTER_CRITICAL(&mux_);
  if (!collectCandidates_) {
    portEXIT_CRITICAL(&mux_);
    return;
  }
  if (advertisement.connectable) incrementSaturating(connectableFragments_);
  if (hasIdentity) incrementSaturating(namedFragments_);
  if (micraIdentity) incrementSaturating(micraFragments_);
  portEXIT_CRITICAL(&mux_);
  if (!printableIdentity) {
    noteMalformed();
    return;
  }
  if ((hasIdentity && !micraIdentity) ||
      (!hasIdentity && !advertisement.connectable)) return;

  portENTER_CRITICAL(&mux_);
  if (!collectCandidates_) {
    portEXIT_CRITICAL(&mux_);
    return;
  }
  if (!micraIdentity) {
    const uint32_t key = peerKey(advertisement.addressType,
                                 advertisement.address);
    for (uint32_t rejectedKey : rejectedPeerKeys_) {
      if (rejectedKey == key) {
        portEXIT_CRITICAL(&mux_);
        return;
      }
    }
  }
  Candidate *slot = nullptr;
  for (Candidate &candidate : candidates_) {
    if (candidate.used && candidate.addressType == advertisement.addressType &&
        memcmp(candidate.address, advertisement.address,
               sizeof(candidate.address)) == 0) {
      slot = &candidate;
      break;
    }
    if (!candidate.used && slot == nullptr) slot = &candidate;
  }
  if (slot == nullptr) {
    Candidate *weakestAnonymous = nullptr;
    for (Candidate &candidate : candidates_) {
      if (candidate.identity[0] == '\0' &&
          (weakestAnonymous == nullptr ||
           candidate.rssi < weakestAnonymous->rssi)) {
        weakestAnonymous = &candidate;
      }
    }
    if (weakestAnonymous == nullptr ||
        (!micraIdentity && advertisement.rssi <= weakestAnonymous->rssi)) {
      portEXIT_CRITICAL(&mux_);
      return;
    }
    slot = weakestAnonymous;
  }
  if (!slot->used || slot->addressType != advertisement.addressType ||
      memcmp(slot->address, advertisement.address,
             sizeof(slot->address)) != 0) {
    *slot = {};
  }
  memcpy(slot->address, advertisement.address, sizeof(slot->address));
  if (hasIdentity) memcpy(slot->identity, identity, sizeof(slot->identity));
  slot->rssi = advertisement.rssi;
  slot->addressType = advertisement.addressType;
  slot->connectable |= advertisement.connectable;
  slot->used = true;
  portEXIT_CRITICAL(&mux_);
}

}  // namespace shotstopper
