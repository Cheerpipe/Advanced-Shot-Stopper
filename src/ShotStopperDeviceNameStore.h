#pragma once

#include "ShotStopperDeviceName.h"
#include "ShotStopperFlashIoScratch.h"
#include "ShotStopperPersistedSettings.h"
#include "ShotStopperPreferences.h"

namespace shotstopper {

constexpr const char *DEVICE_NAME_KEY = "deviceName";

// Independent optional NVS blob: legacy settings need no layout migration.
inline bool loadDeviceName(char (&name)[DEVICE_NAME_CAPACITY]) {
  strcpy(name, DEFAULT_DEVICE_NAME);
  if (!lockFlashIo()) return false;
  ShotStopperPreferences preferences(NvsSubsystem::SETTINGS);
  char stored[DEVICE_NAME_CAPACITY] = {};
  const bool opened = preferences.begin(SETTINGS_NAMESPACE, true);
  const bool present = opened && preferences.isKey(DEVICE_NAME_KEY);
  const bool valid = present &&
      preferences.getBytesLength(DEVICE_NAME_KEY) == sizeof(stored) &&
      preferences.getBytes(DEVICE_NAME_KEY, stored, sizeof(stored)) == sizeof(stored) &&
      validDeviceName(stored);
  if (valid) memcpy(name, stored, sizeof(stored));
  preferences.end();
  unlockFlashIo();
  return opened && (!present || valid);
}

inline bool saveDeviceName(const char *name) {
  if (!validDeviceName(name)) return false;
  // Flash writes require an internal-memory source even if the caller uses PSRAM.
  char stored[DEVICE_NAME_CAPACITY] = {};
  strcpy(stored, name);
  char verified[DEVICE_NAME_CAPACITY] = {};
  if (!lockFlashIo()) return false;
  ShotStopperPreferences preferences(NvsSubsystem::SETTINGS);
  const bool saved = preferences.begin(SETTINGS_NAMESPACE, false) &&
      preferences.putBytes(DEVICE_NAME_KEY, stored, sizeof(stored)) == sizeof(stored) &&
      preferences.getBytes(DEVICE_NAME_KEY, verified, sizeof(verified)) == sizeof(verified) &&
      memcmp(stored, verified, sizeof(stored)) == 0;
  preferences.end();
  unlockFlashIo();
  return saved;
}

}  // namespace shotstopper
