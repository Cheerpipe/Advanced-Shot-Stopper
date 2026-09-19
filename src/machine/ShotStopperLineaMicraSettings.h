#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace shotstopper {

constexpr size_t LINEA_MICRA_TOKEN_CAPACITY = 65;
constexpr size_t LINEA_MICRA_IDENTITY_CAPACITY = 24;
constexpr uint8_t LINEA_MICRA_APPLY_TEMPERATURE = 1U << 0;
constexpr uint8_t LINEA_MICRA_OBSERVE_STATE = 1U << 1;
constexpr uint16_t LINEA_MICRA_BREW_TARGET_MIN_DECI_C = 800;
constexpr uint16_t LINEA_MICRA_BREW_TARGET_MAX_DECI_C = 1000;
constexpr uint16_t LINEA_MICRA_BREW_TARGET_DEFAULT_DECI_C = 930;

// Frozen V15 record. It remains in the shared blob for cross-profile storage
// compatibility, but only the selected Linea Micra adapter interprets it.
struct LineaMicraPersistedSettings {
  char token[LINEA_MICRA_TOKEN_CAPACITY] = {};
  uint8_t peerAddress[6] = {};
  char identity[LINEA_MICRA_IDENTITY_CAPACITY] = {};
  uint8_t peerAddressType = 0;
  uint8_t options = 0;
  bool bindingVerified = false;
};

inline bool validLineaMicraSettings(
    const LineaMicraPersistedSettings &settings) {
  const size_t tokenLength = strnlen(settings.token, sizeof(settings.token));
  if (tokenLength != 0 && tokenLength != 64) return false;
  for (size_t i = 0; i < tokenLength; ++i) {
    const uint8_t value = static_cast<uint8_t>(settings.token[i]);
    if (value < 0x20 || value > 0x7e) return false;
  }
  if ((settings.options &
       ~(LINEA_MICRA_APPLY_TEMPERATURE | LINEA_MICRA_OBSERVE_STATE)) != 0) {
    return false;
  }
  const size_t identityLength =
      strnlen(settings.identity, sizeof(settings.identity));
  if (identityLength >= sizeof(settings.identity) ||
      settings.peerAddressType > 3) {
    return false;
  }
  for (size_t i = 0; i < identityLength; ++i) {
    const uint8_t value = static_cast<uint8_t>(settings.identity[i]);
    if (value < 0x20 || value > 0x7e) return false;
  }
  if (!settings.bindingVerified) return true;
  if (tokenLength != 64 || identityLength == 0) return false;
  bool nonzeroAddress = false;
  for (uint8_t value : settings.peerAddress) nonzeroAddress |= value != 0;
  return nonzeroAddress;
}

inline void clearLineaMicraBinding(LineaMicraPersistedSettings &settings) {
  memset(settings.peerAddress, 0, sizeof(settings.peerAddress));
  memset(settings.identity, 0, sizeof(settings.identity));
  settings.peerAddressType = 0;
  settings.bindingVerified = false;
}

inline void clearLineaMicraToken(LineaMicraPersistedSettings &settings) {
  memset(settings.token, 0, sizeof(settings.token));
  clearLineaMicraBinding(settings);
}

inline bool setLineaMicraToken(LineaMicraPersistedSettings &settings,
                               const char *token, size_t length) {
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
  clearLineaMicraBinding(settings);
  return true;
}

inline bool setLineaMicraOptions(LineaMicraPersistedSettings &settings,
                                 bool applyTemperature, bool observeState) {
  settings.options =
      (applyTemperature ? LINEA_MICRA_APPLY_TEMPERATURE : 0U) |
      (observeState ? LINEA_MICRA_OBSERVE_STATE : 0U);
  return true;
}

inline bool setLineaMicraBinding(LineaMicraPersistedSettings &settings,
                                 uint8_t addressType,
                                 const uint8_t address[6],
                                 const char *identity) {
  if (address == nullptr || identity == nullptr || addressType > 3) return false;
  const size_t identityLength =
      strnlen(identity, LINEA_MICRA_IDENTITY_CAPACITY);
  if (identityLength == 0 || identityLength >= LINEA_MICRA_IDENTITY_CAPACITY) {
    return false;
  }
  LineaMicraPersistedSettings candidate = settings;
  memcpy(candidate.peerAddress, address, sizeof(candidate.peerAddress));
  memset(candidate.identity, 0, sizeof(candidate.identity));
  memcpy(candidate.identity, identity, identityLength);
  candidate.peerAddressType = addressType;
  candidate.bindingVerified = true;
  if (!validLineaMicraSettings(candidate)) return false;
  settings = candidate;
  return true;
}

static_assert(sizeof(LineaMicraPersistedSettings) == 98,
              "V15 Linea Micra settings ABI changed");

}  // namespace shotstopper
