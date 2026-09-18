#pragma once

#include "ShotStopperNvsDualSlot.h"
#include "ShotStopperPersistence.h"

namespace shotstopper {

// On-disk keys and magic stay BLEC/bleCfg* so V1 Companion blobs upgrade
// without renaming NVS entries. Version 2 stopped using the Companion enable
// flag; that byte is reserved and always written 0. Version 3 turned one
// reserved byte into scanBackoffMin without changing the blob size, so V2
// slots stay readable and upgrade to the default backoff (now OFF).
constexpr uint32_t BLE_SCAN_SETTINGS_MAGIC = 0x424C4543U;  // "BLEC"
constexpr uint16_t BLE_SCAN_SETTINGS_VERSION = 3;
constexpr uint16_t BLE_SCAN_SETTINGS_V2_VERSION = 2;
constexpr uint16_t BLE_SCAN_SETTINGS_V1_VERSION = 1;
constexpr const char *BLE_SCAN_SLOT_A = "bleCfgA";
constexpr const char *BLE_SCAN_SLOT_B = "bleCfgB";

struct BleScanPersistedSettings {
  uint32_t magic = BLE_SCAN_SETTINGS_MAGIC;
  uint16_t version = BLE_SCAN_SETTINGS_VERSION;
  uint16_t structureSize = sizeof(BleScanPersistedSettings);
  uint32_t revision = 0;
  uint8_t reservedEnabled = 0;
  uint8_t scanIntensity = static_cast<uint8_t>(BleScanIntensity::BALANCED);
  uint8_t scanBackoffMin = SCALE_SCAN_QUIET_BACKOFF_DEFAULT_MIN;
  uint8_t reserved[1] = {};
  uint32_t checksum = 0;
};

inline uint32_t bleScanSettingsChecksum(
    const BleScanPersistedSettings &settings) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&settings);
  uint32_t hash = 2166136261U;
  for (size_t index = 0;
       index < offsetof(BleScanPersistedSettings, checksum); ++index) {
    hash ^= bytes[index];
    hash *= 16777619U;
  }
  return hash;
}

inline void finalizeBleScanSettings(BleScanPersistedSettings &settings) {
  if (settings.version < BLE_SCAN_SETTINGS_VERSION) {
    // Older blobs kept this byte reserved (always 0): adopt the default
    // backoff rather than trusting the reserved zero as a stored choice.
    settings.scanBackoffMin = SCALE_SCAN_QUIET_BACKOFF_DEFAULT_MIN;
  }
  settings.magic = BLE_SCAN_SETTINGS_MAGIC;
  settings.version = BLE_SCAN_SETTINGS_VERSION;
  settings.structureSize = sizeof(BleScanPersistedSettings);
  settings.reservedEnabled = 0;
  settings.scanIntensity =
      static_cast<uint8_t>(clampBleScanIntensity(settings.scanIntensity));
  settings.scanBackoffMin = clampBleScanBackoffMin(settings.scanBackoffMin);
  settings.checksum = 0;
  settings.checksum = bleScanSettingsChecksum(settings);
}

inline bool validBleScanSettingsBlob(const BleScanPersistedSettings &settings) {
  if (settings.magic != BLE_SCAN_SETTINGS_MAGIC ||
      settings.structureSize != sizeof(BleScanPersistedSettings) ||
      !validBleScanIntensity(settings.scanIntensity) ||
      settings.checksum != bleScanSettingsChecksum(settings)) {
    return false;
  }
  if (settings.version == BLE_SCAN_SETTINGS_V1_VERSION) {
    return settings.reservedEnabled <= 1;
  }
  if (settings.version == BLE_SCAN_SETTINGS_V2_VERSION) {
    return true;
  }
  return settings.version == BLE_SCAN_SETTINGS_VERSION &&
         validBleScanBackoffMin(settings.scanBackoffMin);
}

inline bool readBleScanSlot(ShotStopperPreferences &preferences, const char *key,
                            BleScanPersistedSettings &settings) {
  if (!preferences.isKey(key) ||
      preferences.getBytesLength(key) != sizeof(settings) ||
      preferences.getBytes(key, &settings, sizeof(settings)) !=
          sizeof(settings)) {
    return false;
  }
  return validBleScanSettingsBlob(settings);
}

inline bool readLatestBleScanSettings(BleScanPersistedSettings &settings) {
  if (!lockSettingsNvs()) {
    return false;
  }
  ShotStopperPreferences preferences(NvsSubsystem::BLE_SCAN);
  if (!preferences.begin(SETTINGS_NAMESPACE, true)) {
    unlockSettingsNvs();
    return false;
  }
  BleScanPersistedSettings first;
  BleScanPersistedSettings second;
  const bool firstValid = readBleScanSlot(preferences, BLE_SCAN_SLOT_A, first);
  const bool secondValid =
      readBleScanSlot(preferences, BLE_SCAN_SLOT_B, second);
  preferences.end();
  const DualSlotChoice choice = chooseNewerRevision(
      firstValid, first.revision, secondValid, second.revision);
  bool loaded = false;
  if (choice == DualSlotChoice::SECOND) {
    settings = second;
    loaded = true;
  } else if (choice == DualSlotChoice::FIRST) {
    settings = first;
    loaded = true;
  }
  unlockSettingsNvs();
  return loaded;
}

inline bool loadBleScanSettings(BleScanPersistedSettings &settings) {
  if (!readLatestBleScanSettings(settings)) {
    return false;
  }
  finalizeBleScanSettings(settings);
  return true;
}

inline bool verifyFactoryBleScanSettings(
    const BleScanPersistedSettings &settings) {
  return validBleScanSettingsBlob(settings) &&
         settings.version == BLE_SCAN_SETTINGS_VERSION &&
         settings.reservedEnabled == 0 &&
         settings.scanIntensity ==
             static_cast<uint8_t>(BleScanIntensity::BALANCED) &&
         settings.scanBackoffMin == SCALE_SCAN_QUIET_BACKOFF_DEFAULT_MIN;
}

inline bool saveBleScanSettings(BleScanPersistedSettings &settings) {
  if (!lockSettingsNvs()) {
    return false;
  }
  BleScanPersistedSettings current;
  if (readLatestBleScanSettings(current)) {
    settings.revision = current.revision;
  }
  ++settings.revision;
  if (settings.revision == 0) {
    settings.revision = 1;
  }
  finalizeBleScanSettings(settings);
  ShotStopperPreferences preferences(NvsSubsystem::BLE_SCAN);
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    unlockSettingsNvs();
    return false;
  }
  const char *target = (settings.revision & 1U) != 0 ? BLE_SCAN_SLOT_A
                                                     : BLE_SCAN_SLOT_B;
  const bool written =
      preferences.putBytes(target, &settings, sizeof(settings)) ==
      sizeof(settings);
  BleScanPersistedSettings verified;
  const bool saved = written && readBleScanSlot(preferences, target, verified) &&
                     verified.revision == settings.revision;
  preferences.end();
  unlockSettingsNvs();
  return saved;
}

// One dual-slot save covers both fields: a request that changes intensity and
// backoff together must not cost two flash writes and two revisions. An
// unchanged field is passed through by the caller and writes nothing.
inline bool persistBleScanSettings(BleScanPersistedSettings &settings,
                                   uint8_t intensity, uint8_t backoffMin) {
  const uint8_t storedIntensity =
      static_cast<uint8_t>(clampBleScanIntensity(intensity));
  const uint8_t storedBackoff = clampBleScanBackoffMin(backoffMin);
  if (settings.scanIntensity == storedIntensity &&
      settings.scanBackoffMin == storedBackoff) {
    return true;
  }
  BleScanPersistedSettings candidate = settings;
  candidate.scanIntensity = storedIntensity;
  candidate.scanBackoffMin = storedBackoff;
  if (!saveBleScanSettings(candidate)) {
    return false;
  }
  settings = candidate;
  return true;
}

inline bool resetBleScanSettings(BleScanPersistedSettings &settings) {
  settings = BleScanPersistedSettings{};
  return saveBleScanSettings(settings);
}

}  // namespace shotstopper
