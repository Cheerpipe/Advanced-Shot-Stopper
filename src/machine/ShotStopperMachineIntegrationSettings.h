#pragma once

#include "ShotStopperPersistedSettings.h"

#include <cstddef>
#include <cstring>

namespace shotstopper {

inline void clearMachineIntegrationBinding(
    MachineIntegrationPersistedSettings &settings) {
  memset(settings.peerAddress, 0, sizeof(settings.peerAddress));
  memset(settings.identity, 0, sizeof(settings.identity));
  settings.peerAddressType = 0;
  settings.bindingVerified = false;
}

inline void clearMachineIntegrationToken(
    MachineIntegrationPersistedSettings &settings) {
  memset(settings.token, 0, sizeof(settings.token));
  clearMachineIntegrationBinding(settings);
}

inline bool setMachineIntegrationToken(
    MachineIntegrationPersistedSettings &settings, const char *token,
    size_t length) {
  if (token == nullptr || length != 64) return false;
  for (size_t i = 0; i < length; ++i) {
    const uint8_t value = static_cast<uint8_t>(token[i]);
    if (value < 0x20 || value > 0x7e) return false;
  }
  if (memcmp(settings.token, token, length) == 0 &&
      settings.token[length] == '\0') {
    return true;
  }
  memset(settings.token, 0, sizeof(settings.token));
  memcpy(settings.token, token, length);
  clearMachineIntegrationBinding(settings);
  return true;
}

inline bool setMachineIntegrationOptions(
    MachineIntegrationPersistedSettings &settings, bool applyTemperature,
    bool observeState) {
  settings.options =
      (applyTemperature ? MACHINE_INTEGRATION_APPLY_TEMPERATURE : 0U) |
      (observeState ? MACHINE_INTEGRATION_OBSERVE_STATE : 0U);
  return true;
}

inline bool setMachineIntegrationBinding(
    MachineIntegrationPersistedSettings &settings, uint8_t addressType,
    const uint8_t address[6], const char *identity) {
  if (address == nullptr || identity == nullptr || addressType > 3) return false;
  const size_t identityLength = strnlen(identity, MICRA_IDENTITY_CAPACITY);
  if (identityLength == 0 || identityLength >= MICRA_IDENTITY_CAPACITY) {
    return false;
  }
  MachineIntegrationPersistedSettings candidate = settings;
  memcpy(candidate.peerAddress, address, sizeof(candidate.peerAddress));
  memset(candidate.identity, 0, sizeof(candidate.identity));
  memcpy(candidate.identity, identity, identityLength);
  candidate.peerAddressType = addressType;
  candidate.bindingVerified = true;
  if (!validMachineIntegrationSettings(candidate)) return false;
  settings = candidate;
  return true;
}

}  // namespace shotstopper
