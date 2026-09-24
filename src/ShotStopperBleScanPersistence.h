#pragma once

#include "ShotStopperNvsDualSlot.h"
#include "ShotStopperPersistence.h"

namespace shotstopper {

// The BLE scan store has one fixed layout. Version 2 gave the reserved byte
// the master-switch meaning (enabled); valid version 1 blobs are upgraded to
// enabled BLE in memory so an update never switches a user's scales off.
constexpr uint32_t BLE_SCAN_SETTINGS_MAGIC = 0x424C4543U;  // "BLEC"
constexpr uint16_t BLE_SCAN_SETTINGS_VERSION = 2;
constexpr const char *BLE_SCAN_SLOT_A = "bleCfgA";
constexpr const char *BLE_SCAN_SLOT_B = "bleCfgB";

struct BleScanPersistedSettings {
  uint32_t magic = BLE_SCAN_SETTINGS_MAGIC;
  uint16_t version = BLE_SCAN_SETTINGS_VERSION;
  uint16_t structureSize = sizeof(BleScanPersistedSettings);
  uint32_t revision = 0;
  // BLE master switch: 1 (factory default) keeps scale Bluetooth active.
  uint8_t enabled = 1;
  uint8_t scanIntensity = static_cast<uint8_t>(BLE_SCAN_FACTORY_INTENSITY);
  uint8_t scanBackoffMin = SCALE_SCAN_QUIET_BACKOFF_DEFAULT_MIN;
  uint8_t scanBoostMin = SCALE_SCAN_BOOST_DEFAULT_MIN;
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
  settings.magic = BLE_SCAN_SETTINGS_MAGIC;
  settings.version = BLE_SCAN_SETTINGS_VERSION;
  settings.structureSize = sizeof(BleScanPersistedSettings);
  settings.enabled = settings.enabled != 0 ? 1 : 0;
  settings.scanIntensity =
      static_cast<uint8_t>(clampBleScanIntensity(settings.scanIntensity));
  settings.scanBackoffMin = clampBleScanBackoffMin(settings.scanBackoffMin);
  settings.scanBoostMin = clampBleScanBoostMin(settings.scanBoostMin);
  settings.checksum = 0;
  settings.checksum = bleScanSettingsChecksum(settings);
}

inline bool validBleScanSettingsBlob(const BleScanPersistedSettings &settings) {
  if (settings.magic != BLE_SCAN_SETTINGS_MAGIC ||
      settings.structureSize != sizeof(BleScanPersistedSettings) ||
      !validBleScanIntensity(settings.scanIntensity) ||
      !validBleScanBackoffMin(settings.scanBackoffMin) ||
      !validBleScanBoostMin(settings.scanBoostMin) ||
      settings.checksum != bleScanSettingsChecksum(settings)) {
    return false;
  }
  // Version 1 blobs predate the master switch: their reserved byte is 0,
  // which upgrades to enabled rather than meaning "disabled".
  if (settings.version == BLE_SCAN_SETTINGS_VERSION - 1) {
    return settings.enabled == 0;
  }
  return settings.version == BLE_SCAN_SETTINGS_VERSION &&
         settings.enabled <= 1;
}

inline bool readBleScanSlot(ShotStopperPreferences &preferences, const char *key,
                            BleScanPersistedSettings &settings) {
  if (!preferences.isKey(key) ||
      preferences.getBytesLength(key) != sizeof(settings) ||
      preferences.getBytes(key, &settings, sizeof(settings)) !=
          sizeof(settings)) {
    return false;
  }
  if (!validBleScanSettingsBlob(settings)) {
    return false;
  }
  if (settings.version != BLE_SCAN_SETTINGS_VERSION) {
    settings.version = BLE_SCAN_SETTINGS_VERSION;
    settings.enabled = 1;
  }
  return true;
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
         settings.enabled == 1 &&
         settings.scanIntensity ==
             static_cast<uint8_t>(BLE_SCAN_FACTORY_INTENSITY) &&
         settings.scanBackoffMin == SCALE_SCAN_QUIET_BACKOFF_DEFAULT_MIN &&
         settings.scanBoostMin == SCALE_SCAN_BOOST_DEFAULT_MIN;
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

// One dual-slot save covers all four fields: a request that changes several
// of them together must not cost several flash writes and revisions. An
// unchanged field is passed through by the caller and writes nothing.
inline bool persistBleScanSettings(BleScanPersistedSettings &settings,
                                   uint8_t intensity, uint8_t backoffMin,
                                   uint8_t boostMin, uint8_t enabled) {
  const uint8_t storedIntensity =
      static_cast<uint8_t>(clampBleScanIntensity(intensity));
  const uint8_t storedBackoff = clampBleScanBackoffMin(backoffMin);
  const uint8_t storedBoost = clampBleScanBoostMin(boostMin);
  const uint8_t storedEnabled = enabled != 0 ? 1 : 0;
  if (settings.scanIntensity == storedIntensity &&
      settings.scanBackoffMin == storedBackoff &&
      settings.scanBoostMin == storedBoost &&
      settings.enabled == storedEnabled) {
    return true;
  }
  BleScanPersistedSettings candidate = settings;
  candidate.scanIntensity = storedIntensity;
  candidate.scanBackoffMin = storedBackoff;
  candidate.scanBoostMin = storedBoost;
  candidate.enabled = storedEnabled;
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
