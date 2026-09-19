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
    client_.abort();
    config_ = nextConfig;
    configGeneration_ = nextConfigGeneration;
    request_ = {};
    workingStatus_ = {};
    workingStatus_.configGeneration = configGeneration_;
    const bool configured = strnlen(config_.token, sizeof(config_.token)) == 64;
    workingStatus_.phase = configured ? LineaMicraPhase::IDLE
                                      : LineaMicraPhase::Disabled;
    workingStatus_.quality = configured
                                 ? LineaMicraObservationQuality::UNCONFIGURED
                                 : LineaMicraObservationQuality::Disabled;
    stage_ = Stage::IDLE;
    publishStatus();
  }
  client_.service();
  lineamicra::ClientEvent event;
  if (client_.takeEvent(event)) handleClientEvent(event);
  if (stage_ == Stage::DISCONNECT &&
      client_.state() == lineamicra::ClientState::IDLE) {
    stage_ = Stage::IDLE;
  }

  bool start = false;
  if (stage_ == Stage::IDLE &&
      client_.state() == lineamicra::ClientState::IDLE) {
    portENTER_CRITICAL(&mux_);
    if (requestPending_) {
      request_ = pendingRequest_;
      requestPending_ = false;
      start = true;
    }
    portEXIT_CRITICAL(&mux_);
  }
  if (start) {
    workingStatus_.requestId = request_.requestId;
    workingStatus_.configGeneration = request_.configGeneration;
    workingStatus_.activityGeneration = request_.activityGeneration;
    workingStatus_.phase = LineaMicraPhase::QUEUED;
    workingStatus_.measuredValid = false;
    workingStatus_.targetValid = false;
    const bool collect = request_.type == LineaMicraRequestType::TEST &&
                         !config_.bindingVerified;
    portENTER_CRITICAL(&mux_);
    collectCandidates_ = collect;
    if (collect) memset(candidates_, 0, sizeof(candidates_));
    portEXIT_CRITICAL(&mux_);
    requestStartedAtMs_ = millis();
    stage_ = collect ? Stage::WAIT_CANDIDATES : Stage::CONNECT;
    start = !collect;
    publishStatus();
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
    for (const Candidate &candidate : candidates_) {
      if (candidate.used &&
          (!found || candidate.rssi > best.rssi ||
           (candidate.rssi == best.rssi &&
            memcmp(candidate.address, best.address,
                   sizeof(candidate.address)) < 0))) {
        best = candidate;
        found = true;
      }
    }
    portEXIT_CRITICAL(&mux_);
    if (!found) {
      workingStatus_.phase = LineaMicraPhase::FAILED;
      workingStatus_.quality =
          LineaMicraObservationQuality::COMMUNICATION_ERROR;
      stage_ = Stage::IDLE;
      publishStatus();
      return;
    }
    peer.type = best.addressType;
    memcpy(peer.value, best.address, sizeof(peer.value));
  }
  workingStatus_.phase = LineaMicraPhase::RUNNING;
  publishStatus();
  stage_ = Stage::CONNECT;
  if (!client_.connect(peer,
                       {micra_timing::kConnectTimeoutMs,
                        micra_timing::kAttTimeoutMs,
                        micra_timing::kConnectedSessionMaxMs},
                       request_.requestId)) {
    failRequest();
  }
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
      workingStatus_.phase = LineaMicraPhase::FAILED;
      workingStatus_.quality =
          LineaMicraObservationQuality::COMMUNICATION_ERROR;
      workingStatus_.powerState = LineaMicraPowerState::UNKNOWN;
      workingStatus_.effectiveOn = true;
      publishStatus();
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
    if (!client_.authenticate(config_.token, 64, request_.requestId)) {
      failRequest();
    }
    return;
  }
  if (event.type == lineamicra::EventType::AUTHENTICATED) {
    if (request_.type == LineaMicraRequestType::OBSERVE_STATE) {
      stage_ = Stage::MODE;
      if (!client_.query(lineamicra::Query::MACHINE_MODE,
                         request_.requestId)) failRequest();
    } else if (!config_.bindingVerified) {
      stage_ = Stage::CAPABILITIES;
      if (!client_.query(lineamicra::Query::MACHINE_CAPABILITIES,
                         request_.requestId)) failRequest();
    } else {
      stage_ = Stage::BOILERS;
      if (!client_.query(lineamicra::Query::BOILERS,
                         request_.requestId)) failRequest();
    }
    return;
  }
  if (event.type != lineamicra::EventType::RESPONSE) return;
  lineamicra::Error error = lineamicra::Error::NONE;
  if (stage_ == Stage::CAPABILITIES) {
    if (lineamicra::parseMachineCapabilities(event.payload, event.payloadLength,
                                              error)) {
      stage_ = Stage::BOILERS;
      if (!client_.query(lineamicra::Query::BOILERS,
                         request_.requestId)) failRequest();
      return;
    }
  } else if (stage_ == Stage::BOILERS) {
    lineamicra::BrewBoiler boiler;
    if (lineamicra::parseBrewBoiler(event.payload, event.payloadLength, boiler,
                                    error)) {
      workingStatus_.measuredDeciC = boiler.currentDeciC;
      workingStatus_.targetDeciC = boiler.targetDeciC;
      workingStatus_.measuredValid = workingStatus_.targetValid = true;
      workingStatus_.sampleAtMs = millis();
      workingStatus_.phase = LineaMicraPhase::CONFIRMED;
      publishStatus();
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
      client_.disconnect();
      stage_ = Stage::DISCONNECT;
      return;
    }
  }
  failRequest();
}

void ShotStopperMicraService::failRequest() {
  workingStatus_.phase = LineaMicraPhase::FAILED;
  workingStatus_.quality = LineaMicraObservationQuality::COMMUNICATION_ERROR;
  workingStatus_.powerState = LineaMicraPowerState::UNKNOWN;
  workingStatus_.effectiveOn = true;
  publishStatus();
  client_.disconnect();
  stage_ = client_.state() == lineamicra::ClientState::IDLE
               ? Stage::IDLE
               : Stage::DISCONNECT;
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
    requestPending_ = true;
  }
  portEXIT_CRITICAL(&mux_);
  return accepted;
}

LineaMicraStatus ShotStopperMicraService::status() const {
  portENTER_CRITICAL(&mux_);
  const LineaMicraStatus snapshot = publishedStatus_;
  portEXIT_CRITICAL(&mux_);
  return snapshot;
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
