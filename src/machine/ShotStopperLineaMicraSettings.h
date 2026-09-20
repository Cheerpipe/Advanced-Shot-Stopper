#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace shotstopper {

constexpr size_t LINEA_MICRA_USERNAME_CAPACITY = 97;
constexpr size_t LINEA_MICRA_PASSWORD_CAPACITY = 97;
constexpr size_t LINEA_MICRA_SERIAL_CAPACITY = 33;
constexpr size_t LINEA_MICRA_NAME_CAPACITY = 49;
constexpr size_t LINEA_MICRA_PRIVATE_KEY_BYTES = 32;
constexpr uint8_t LINEA_MICRA_APPLY_TEMPERATURE = 1U << 0;
constexpr uint8_t LINEA_MICRA_OBSERVE_STATE = 1U << 1;
constexpr uint16_t LINEA_MICRA_BREW_TARGET_MIN_DECI_C = 800;
constexpr uint16_t LINEA_MICRA_BREW_TARGET_MAX_DECI_C = 1000;
constexpr uint16_t LINEA_MICRA_BREW_TARGET_DEFAULT_DECI_C = 930;

// V16 cloud account record. Access and refresh tokens are deliberately absent:
// they are short-lived worker state and are never written to flash.
struct LineaMicraPersistedSettings {
  char username[LINEA_MICRA_USERNAME_CAPACITY] = {};
  char password[LINEA_MICRA_PASSWORD_CAPACITY] = {};
  uint8_t installationPrivateKey[LINEA_MICRA_PRIVATE_KEY_BYTES] = {};
  char selectedSerial[LINEA_MICRA_SERIAL_CAPACITY] = {};
  char selectedName[LINEA_MICRA_NAME_CAPACITY] = {};
  uint8_t options = 0;
  bool accountConfigured = false;
};

inline bool lineaMicraBoundedText(const char *value, size_t capacity,
                                  bool allowEmpty) {
  if (value == nullptr || capacity < 2) return false;
  const size_t length = strnlen(value, capacity);
  if (length >= capacity || (!allowEmpty && length == 0)) return false;
  for (size_t i = 0; i < length; ++i) {
    const uint8_t byte = static_cast<uint8_t>(value[i]);
    if (byte < 0x20 || byte == 0x7f) return false;
  }
  return true;
}

inline bool lineaMicraPrivateKeyConfigured(
    const LineaMicraPersistedSettings &settings) {
  uint8_t combined = 0;
  for (uint8_t byte : settings.installationPrivateKey) combined |= byte;
  return combined != 0;
}

inline bool validLineaMicraSerial(const char *serial) {
  if (!lineaMicraBoundedText(serial, LINEA_MICRA_SERIAL_CAPACITY, false)) {
    return false;
  }
  for (size_t i = 0; serial[i] != '\0'; ++i) {
    const char value = serial[i];
    if (!((value >= 'A' && value <= 'Z') ||
          (value >= 'a' && value <= 'z') ||
          (value >= '0' && value <= '9') || value == '-' || value == '_')) {
      return false;
    }
  }
  return true;
}

inline bool validLineaMicraSettings(
    const LineaMicraPersistedSettings &settings) {
  if ((settings.options &
       ~(LINEA_MICRA_APPLY_TEMPERATURE | LINEA_MICRA_OBSERVE_STATE)) != 0) {
    return false;
  }
  if (!settings.accountConfigured) {
    return settings.username[0] == '\0' && settings.password[0] == '\0' &&
           settings.selectedSerial[0] == '\0' &&
           settings.selectedName[0] == '\0' &&
           !lineaMicraPrivateKeyConfigured(settings);
  }
  const char *at = strchr(settings.username, '@');
  return lineaMicraBoundedText(settings.username, sizeof(settings.username),
                               false) &&
         at != nullptr && at != settings.username && at[1] != '\0' &&
         lineaMicraBoundedText(settings.password, sizeof(settings.password),
                               false) &&
         lineaMicraPrivateKeyConfigured(settings) &&
         validLineaMicraSerial(settings.selectedSerial) &&
         lineaMicraBoundedText(settings.selectedName,
                               sizeof(settings.selectedName), true);
}

inline void wipeLineaMicraSettings(LineaMicraPersistedSettings &settings) {
  volatile uint8_t *bytes = reinterpret_cast<volatile uint8_t *>(&settings);
  for (size_t index = 0; index < sizeof(settings); ++index) bytes[index] = 0;
}

inline void disconnectLineaMicra(LineaMicraPersistedSettings &settings) {
  const uint8_t options = settings.options;
  wipeLineaMicraSettings(settings);
  settings.options = options;
}

inline bool setLineaMicraOptions(LineaMicraPersistedSettings &settings,
                                 bool applyTemperature, bool observeState) {
  settings.options =
      (applyTemperature ? LINEA_MICRA_APPLY_TEMPERATURE : 0U) |
      (observeState ? LINEA_MICRA_OBSERVE_STATE : 0U);
  return true;
}

static_assert(sizeof(LineaMicraPersistedSettings) == 310,
              "V16 Linea Micra cloud settings ABI changed");

}  // namespace shotstopper
