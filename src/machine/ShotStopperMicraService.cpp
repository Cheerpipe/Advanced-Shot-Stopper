#include "ShotStopperMicraService.h"

#include <Arduino.h>
#include <cstring>

namespace shotstopper {

bool ShotStopperMicraService::begin() {
  return shotStopperBleArbiterRegisterObserver(
      ShotStopperBleOwner::Machine, observeAdvertisement, this);
}

void ShotStopperMicraService::service() {
  portENTER_CRITICAL(&mux_);
  const bool configChanged = configChanged_;
  configChanged_ = false;
  portEXIT_CRITICAL(&mux_);
  if (configChanged) client_.abort();
  client_.service();
  lineamicra::ClientEvent event;
  if (client_.takeEvent(event)) handleClientEvent(event);
  portENTER_CRITICAL(&mux_);
  bool start = false;
  if (requestPending_) {
    status_.requestId = request_.requestId;
    status_.configGeneration = request_.configGeneration;
    status_.activityGeneration = request_.activityGeneration;
    status_.phase = MachineIntegrationPhase::QUEUED;
    collectCandidates_ = request_.type == MachineIntegrationRequestType::TEST &&
                         !config_.bindingVerified;
    requestStartedAtMs_ = millis();
    stage_ = collectCandidates_ ? Stage::WAIT_CANDIDATES : Stage::CONNECT;
    requestPending_ = false;
    start = !collectCandidates_;
  }
  portEXIT_CRITICAL(&mux_);
  if (start) startRequest();
  if (stage_ == Stage::WAIT_CANDIDATES &&
      static_cast<uint32_t>(millis() - requestStartedAtMs_) >=
          micra_timing::kDiscoverySliceMs) {
    collectCandidates_ = false;
    startRequest();
  }
}

void ShotStopperMicraService::observeAdvertisement(
    const ShotStopperBleAdvertisement &advertisement, void *context) {
  static_cast<ShotStopperMicraService *>(context)->acceptAdvertisement(
      advertisement);
}

void ShotStopperMicraService::publishConfig(
    const MachineIntegrationPersistedSettings &settings,
    uint32_t configGeneration) {
  portENTER_CRITICAL(&mux_);
  config_ = settings;
  configGeneration_ = configGeneration;
  requestPending_ = false;
  collectCandidates_ = false;
  memset(candidates_, 0, sizeof(candidates_));
  status_ = {};
  status_.configGeneration = configGeneration;
  const bool configured = strnlen(settings.token, sizeof(settings.token)) == 64;
  status_.phase = configured ? MachineIntegrationPhase::IDLE
                             : MachineIntegrationPhase::Disabled;
  status_.quality = configured ? MachineObservationQuality::UNCONFIGURED
                               : MachineObservationQuality::Disabled;
  configChanged_ = true;
  stage_ = Stage::IDLE;
  portEXIT_CRITICAL(&mux_);
}

void ShotStopperMicraService::startRequest() {
  lineamicra::PeerAddress peer;
  if (config_.bindingVerified) {
    peer.type = config_.peerAddressType;
    memcpy(peer.value, config_.peerAddress, sizeof(peer.value));
  } else {
    const Candidate *best = nullptr;
    for (const Candidate &candidate : candidates_) {
      if (candidate.used &&
          (best == nullptr || candidate.rssi > best->rssi ||
           (candidate.rssi == best->rssi &&
            memcmp(candidate.address, best->address,
                   sizeof(candidate.address)) < 0))) {
        best = &candidate;
      }
    }
    if (best == nullptr) {
      status_.phase = MachineIntegrationPhase::FAILED;
      status_.quality = MachineObservationQuality::COMMUNICATION_ERROR;
      stage_ = Stage::IDLE;
      return;
    }
    peer.type = best->addressType;
    memcpy(peer.value, best->address, sizeof(peer.value));
  }
  status_.phase = MachineIntegrationPhase::RUNNING;
  stage_ = Stage::CONNECT;
  if (!client_.connect(peer,
                       {micra_timing::kConnectTimeoutMs,
                        micra_timing::kAttTimeoutMs,
                        micra_timing::kConnectedSessionMaxMs},
                       request_.requestId)) {
    status_.phase = MachineIntegrationPhase::FAILED;
    status_.quality = MachineObservationQuality::COMMUNICATION_ERROR;
    stage_ = Stage::IDLE;
  }
}

void ShotStopperMicraService::handleClientEvent(
    const lineamicra::ClientEvent &event) {
  if (event.requestGeneration != request_.requestId ||
      request_.configGeneration != configGeneration_) {
    return;
  }
  if (event.type == lineamicra::EventType::ERROR) {
    status_.phase = MachineIntegrationPhase::FAILED;
    status_.quality = MachineObservationQuality::COMMUNICATION_ERROR;
    status_.powerState = MachinePowerState::UNKNOWN;
    status_.effectiveOn = true;
    client_.disconnect();
    stage_ = Stage::DISCONNECT;
    return;
  }
  if (event.type == lineamicra::EventType::READY) {
    stage_ = Stage::AUTH;
    if (!client_.authenticate(config_.token, 64, request_.requestId)) {
      status_.phase = MachineIntegrationPhase::FAILED;
    }
    return;
  }
  if (event.type == lineamicra::EventType::AUTHENTICATED) {
    if (request_.type == MachineIntegrationRequestType::OBSERVE_STATE) {
      stage_ = Stage::MODE;
      (void)client_.query(lineamicra::Query::MACHINE_MODE, request_.requestId);
    } else if (!config_.bindingVerified) {
      stage_ = Stage::CAPABILITIES;
      (void)client_.query(lineamicra::Query::MACHINE_CAPABILITIES,
                          request_.requestId);
    } else {
      stage_ = Stage::BOILERS;
      (void)client_.query(lineamicra::Query::BOILERS, request_.requestId);
    }
    return;
  }
  if (event.type != lineamicra::EventType::RESPONSE) return;
  lineamicra::Error error = lineamicra::Error::NONE;
  if (stage_ == Stage::CAPABILITIES) {
    if (lineamicra::parseMachineCapabilities(event.payload, event.payloadLength,
                                              error)) {
      stage_ = Stage::BOILERS;
      (void)client_.query(lineamicra::Query::BOILERS, request_.requestId);
      return;
    }
  } else if (stage_ == Stage::BOILERS) {
    lineamicra::BrewBoiler boiler;
    if (lineamicra::parseBrewBoiler(event.payload, event.payloadLength, boiler,
                                    error)) {
      status_.measuredDeciC = boiler.currentDeciC;
      status_.targetDeciC = boiler.targetDeciC;
      status_.measuredValid = status_.targetValid = true;
      status_.sampleAtMs = millis();
      status_.phase = MachineIntegrationPhase::CONFIRMED;
      client_.disconnect();
      stage_ = Stage::DISCONNECT;
      return;
    }
  } else if (stage_ == Stage::MODE) {
    lineamicra::Mode mode;
    if (lineamicra::parseMachineMode(event.payload, event.payloadLength, mode,
                                     error)) {
      status_.sampleAtMs = millis();
      status_.observedMode =
          mode == lineamicra::Mode::STANDBY
              ? MachineObservedMode::STANDBY
              : (mode == lineamicra::Mode::BREWING
                     ? MachineObservedMode::BREWING
                     : (mode == lineamicra::Mode::ECO
                            ? MachineObservedMode::ECO
                            : MachineObservedMode::UNSUPPORTED));
      status_.powerState = mode == lineamicra::Mode::STANDBY
                               ? MachinePowerState::OFF
                               : (mode == lineamicra::Mode::BREWING
                                      ? MachinePowerState::ON
                                      : MachinePowerState::UNKNOWN);
      status_.effectiveOn = status_.powerState != MachinePowerState::OFF;
      status_.quality = status_.powerState == MachinePowerState::UNKNOWN
                            ? MachineObservationQuality::UNSUPPORTED
                            : MachineObservationQuality::CURRENT;
      status_.phase = MachineIntegrationPhase::CONFIRMED;
      client_.disconnect();
      stage_ = Stage::DISCONNECT;
      return;
    }
  }
  status_.phase = MachineIntegrationPhase::FAILED;
  status_.quality = MachineObservationQuality::COMMUNICATION_ERROR;
  client_.disconnect();
  stage_ = Stage::DISCONNECT;
}

bool ShotStopperMicraService::queue(
    const MachineIntegrationRequest &request) {
  portENTER_CRITICAL(&mux_);
  const bool accepted = request.requestId != 0 &&
                        request.configGeneration == configGeneration_ &&
                        strnlen(config_.token, sizeof(config_.token)) == 64;
  if (accepted) {
    request_ = request;
    requestPending_ = true;
  }
  portEXIT_CRITICAL(&mux_);
  return accepted;
}

MachineIntegrationStatus ShotStopperMicraService::status() const {
  portENTER_CRITICAL(&mux_);
  const MachineIntegrationStatus snapshot = status_;
  portEXIT_CRITICAL(&mux_);
  return snapshot;
}

void ShotStopperMicraService::acceptAdvertisement(
    const ShotStopperBleAdvertisement &advertisement) {
  char identity[MICRA_IDENTITY_CAPACITY] = {};
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
