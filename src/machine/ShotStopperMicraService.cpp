#include "ShotStopperMicraService.h"

#include <Arduino.h>
#include <cstring>

namespace shotstopper {

bool ShotStopperMicraService::begin() {
  return shotStopperBleArbiterRegisterObserver(
      ShotStopperBleOwner::Machine, observeAdvertisement, this);
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
          workingStatus_.phase == LineaMicraPhase::FAILED &&
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
    if (collect) memset(candidates_, 0, sizeof(candidates_));
    portEXIT_CRITICAL(&mux_);
    stage_ = collect ? Stage::WAIT_CANDIDATES : Stage::CONNECT;
    start = !collect;
    publishStatus();
    if (collect &&
        !shotStopperBleArbiterStartObservationWindow(
            micra_timing::kDiscoverySliceMs)) {
      stage_ = Stage::IDLE;
      failRequest();
      return;
    }
  }
  if (start) startRequest();
  if (stage_ == Stage::WAIT_CANDIDATES &&
      static_cast<uint32_t>(millis() - requestStartedAtMs_) >=
          micra_timing::kDiscoverySliceMs) {
    portENTER_CRITICAL(&mux_);
    collectCandidates_ = false;
    portEXIT_CRITICAL(&mux_);
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
  request_.reason = LineaMicraRequestReason::PERIODIC;
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
    Candidate best;
    bool found = false;
    portENTER_CRITICAL(&mux_);
    uint8_t index = 0;
    uint8_t bestIndex = UINT8_MAX;
    for (const Candidate &candidate : candidates_) {
      if (candidate.used &&
          (!found || candidate.rssi > best.rssi ||
           (candidate.rssi == best.rssi &&
            memcmp(candidate.address, best.address,
                   sizeof(candidate.address)) < 0))) {
        best = candidate;
        found = true;
        bestIndex = index;
      }
      ++index;
    }
    portEXIT_CRITICAL(&mux_);
    if (!found) {
      stage_ = Stage::IDLE;
      failRequest();
      return;
    }
    peer.type = best.addressType;
    memcpy(peer.value, best.address, sizeof(peer.value));
    selectedCandidate_ = bestIndex;
  }
  if (!shotStopperBleArbiterPrepareMachineProcedure()) {
    failRequest();
    return;
  }
  workingStatus_.phase = LineaMicraPhase::RUNNING;
  publishStatus();
  stage_ = Stage::CONNECT;
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
      stage_ = Stage::IDLE;
      failRequest();
      return;
    }
    stage_ = Stage::IDLE;
    return;
  }
  if (event.type == lineamicra::EventType::ERROR) {
    failRequest();
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
      if (request_.type == LineaMicraRequestType::TEST &&
          !config_.bindingVerified && selectedCandidate_ < 4) {
        portENTER_CRITICAL(&mux_);
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
  failRequest();
}

void ShotStopperMicraService::handleSubmission(bool accepted) {
  if (accepted) return;
  lineamicra::ClientEvent event;
  if (client_.takeEvent(event)) handleClientEvent(event);
  else failRequest();
}

void ShotStopperMicraService::failRequest() {
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
  if (withinDeadline && attempt_ + 1U < micra_timing::kMaxAttempts) {
    retryAtMs_ = now + micra_timing::kRetryDelaysMs[attempt_] + jitter;
    ++attempt_;
    retryPending_ = true;
    workingStatus_.phase = LineaMicraPhase::BACKOFF;
  } else {
    workingStatus_.phase = LineaMicraPhase::FAILED;
    retryPending_ = false;
    nextObservationAtMs_ = now + micra_timing::kExhaustedCooldownMs + jitter;
    retryAtMs_ = now + micra_timing::kMinDisconnectedMs;
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
  portENTER_CRITICAL(&mux_);
  const bool supported = request.type == LineaMicraRequestType::TEST ||
                         (request.type == LineaMicraRequestType::OBSERVE_STATE &&
                          (pendingConfig_.options &
                           LINEA_MICRA_OBSERVE_STATE) != 0);
  const bool accepted = supported && request.requestId != 0 &&
                        request.configGeneration == acceptedConfigGeneration_ &&
                        strnlen(pendingConfig_.token,
                                sizeof(pendingConfig_.token)) == 64 &&
                        !requestPending_;
  if (accepted) {
    pendingRequest_ = request;
    if (pendingRequest_.type == LineaMicraRequestType::OBSERVE_STATE) {
      pendingRequest_.activityGeneration =
          shotStopperBleArbiterSnapshot().criticalEpoch;
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
  if (effective.quality == LineaMicraObservationQuality::CURRENT &&
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
      !candidates_[selectedCandidate_].used) {
    portEXIT_CRITICAL(&mux_);
    return false;
  }
  const Candidate candidate = candidates_[selectedCandidate_];
  result = {};
  result.requestId = workingStatus_.requestId;
  result.configGeneration = workingStatus_.configGeneration;
  result.addressType = candidate.addressType;
  memcpy(result.address, candidate.address, sizeof(result.address));
  memcpy(result.identity, candidate.identity, sizeof(result.identity));
  portEXIT_CRITICAL(&mux_);
  return true;
}

void ShotStopperMicraService::acceptAdvertisement(
    const ShotStopperBleAdvertisement &advertisement) {
  char identity[LINEA_MICRA_IDENTITY_CAPACITY] = {};
  size_t offset = 0;
  while (advertisement.payload != nullptr &&
         offset < advertisement.payloadLength) {
    const uint8_t fieldLength = advertisement.payload[offset];
    if (fieldLength == 0) break;
    if (offset + fieldLength >= advertisement.payloadLength) return;
    const uint8_t type = advertisement.payload[offset + 1];
    if ((type == 0x08 || type == 0x09) && fieldLength > 1) {
      const size_t length = fieldLength - 1;
      if (length >= sizeof(identity)) return;
      memcpy(identity, advertisement.payload + offset + 2, length);
      identity[length] = '\0';
      break;
    }
    offset += static_cast<size_t>(fieldLength) + 1U;
  }
  if (strncmp(identity, "MICRA_", 6) != 0 || !advertisement.connectable) return;

  portENTER_CRITICAL(&mux_);
  if (!collectCandidates_) {
    portEXIT_CRITICAL(&mux_);
    return;
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
    slot = &candidates_[0];
    for (Candidate &candidate : candidates_) {
      if (candidate.rssi < slot->rssi) slot = &candidate;
    }
    if (advertisement.rssi <= slot->rssi) {
      portEXIT_CRITICAL(&mux_);
      return;
    }
  }
  memcpy(slot->address, advertisement.address, sizeof(slot->address));
  memcpy(slot->identity, identity, sizeof(slot->identity));
  slot->rssi = advertisement.rssi;
  slot->addressType = advertisement.addressType;
  slot->used = true;
  portEXIT_CRITICAL(&mux_);
}

}  // namespace shotstopper
