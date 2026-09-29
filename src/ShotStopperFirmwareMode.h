#pragma once

// Boot-scoped firmware mode (master switch). The persisted record lives in
// its own NVS namespace — deliberately outside the PersistedSettings blob —
// so toggling or upgrading the mode never forces a CONFIG_SCHEMA_VERSION bump
// that would factory-reset every stored setting. An absent or invalid record
// fails safe to FULL firmware mode: a corrupt entry must never silently park
// the controller in compatibility mode.

#include "ShotStopperFlashIoScratch.h"
#include "ShotStopperPreferences.h"

namespace shotstopper {

constexpr const char *FIRMWARE_MODE_NAMESPACE = "fwmode";
constexpr const char *FIRMWARE_MODE_KEY = "mode";

enum class FirmwareMode : uint8_t {
  FULL = 1,
  COMPATIBILITY = 2,
};

inline bool validFirmwareMode(uint8_t raw) {
  return raw == static_cast<uint8_t>(FirmwareMode::FULL) ||
         raw == static_cast<uint8_t>(FirmwareMode::COMPATIBILITY);
}

#if !defined(SHOT_STOPPER_HOST_TEST) ||                                      \
    defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
inline FirmwareMode loadFirmwareMode() {
  if (!lockFlashIo()) {
    return FirmwareMode::FULL;
  }
  FirmwareMode mode = FirmwareMode::FULL;
  ShotStopperPreferences preferences(NvsSubsystem::FIRMWARE_MODE);
  if (preferences.begin(FIRMWARE_MODE_NAMESPACE, true)) {
    uint8_t raw = 0;
    if (preferences.getBytesLength(FIRMWARE_MODE_KEY) == sizeof(raw) &&
        preferences.getBytes(FIRMWARE_MODE_KEY, &raw, sizeof(raw)) ==
            sizeof(raw) &&
        validFirmwareMode(raw)) {
      mode = static_cast<FirmwareMode>(raw);
    }
    preferences.end();
  }
  unlockFlashIo();
  return mode;
}

inline bool saveFirmwareMode(FirmwareMode mode) {
  if (!validFirmwareMode(static_cast<uint8_t>(mode))) {
    return false;
  }
  if (!lockFlashIo()) {
    return false;
  }
  ShotStopperPreferences preferences(NvsSubsystem::FIRMWARE_MODE);
  if (!preferences.begin(FIRMWARE_MODE_NAMESPACE, false)) {
    unlockFlashIo();
    return false;
  }
  const uint8_t raw = static_cast<uint8_t>(mode);
  const bool written =
      preferences.putBytes(FIRMWARE_MODE_KEY, &raw, sizeof(raw)) == sizeof(raw);
  uint8_t verified = 0;
  const bool saved =
      written &&
      preferences.getBytesLength(FIRMWARE_MODE_KEY) == sizeof(verified) &&
      preferences.getBytes(FIRMWARE_MODE_KEY, &verified, sizeof(verified)) ==
          sizeof(verified) &&
      verified == raw;
  preferences.end();
  unlockFlashIo();
  return saved;
}

inline bool clearFirmwareMode() {
  if (!lockFlashIo()) {
    return false;
  }
  ShotStopperPreferences preferences(NvsSubsystem::FIRMWARE_MODE);
  if (!preferences.begin(FIRMWARE_MODE_NAMESPACE, false)) {
    unlockFlashIo();
    return false;
  }
  const bool absent = preferences.getBytesLength(FIRMWARE_MODE_KEY) == 0;
  const bool removed = absent || preferences.remove(FIRMWARE_MODE_KEY);
  const bool verified =
      removed && preferences.getBytesLength(FIRMWARE_MODE_KEY) == 0;
  preferences.end();
  unlockFlashIo();
  return verified;
}
#endif

}  // namespace shotstopper

// Live boot-scoped copy. setup() writes it once from loadFirmwareMode();
// it is immutable for the rest of the boot so every subsystem gate reads one
// stable value with no transition states.
extern uint8_t firmwareModeRaw;

inline bool firmwareCompatibilityMode() {
  return firmwareModeRaw ==
         static_cast<uint8_t>(shotstopper::FirmwareMode::COMPATIBILITY);
}
