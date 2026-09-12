#pragma once

#include "ShotStopperBleCompanionPersistence.h"
#include "ShotStopperLastShot.h"
#include "ShotStopperPersistence.h"
#include "ShotStopperShotCurve.h"
#include "ShotStopperShotLog.h"

namespace shotstopper {

inline bool verifyFactorySettings(const PersistedSettings &settings) {
  if (!validPersistedSettings(settings) || settings.staConfigured ||
      settings.lkgValid || !passwordIsFactoryDefault(settings) ||
      settings.preferredScaleMac[0] != '\0' ||
      settings.preferredScaleName[0] != '\0' ||
      settings.runtime.scaleMacCacheMode !=
          static_cast<uint8_t>(ScaleMacCacheMode::ONLY)) {
    return false;
  }
  for (const ScaleHistoryEntry &entry : settings.scaleHistory) {
    if (entry.mac[0] != '\0' || entry.name[0] != '\0' ||
        entry.lastSeenSeq != 0) {
      return false;
    }
  }
  return true;
}

inline bool resetPersistedNetworkAccess(PersistedSettings &settings) {
  PersistedSettings candidate;
  if (!loadPersistedSettings(candidate) &&
      !initializeDefaultSettings(candidate)) {
    return false;
  }
  clearStaNetwork(candidate);
  if (!initializeDefaultDevicePassword(candidate) ||
      !savePersistedSettings(candidate)) {
    return false;
  }
  PersistedSettings verified;
  if (!loadPersistedSettings(verified) || verified.staConfigured ||
      verified.lkgValid ||
      verified.staIpMode != static_cast<uint8_t>(StaIpMode::DHCP) ||
      !passwordIsFactoryDefault(verified)) {
    return false;
  }
  settings = verified;
  return true;
}

// Erase only expendable NVS records. This is the narrow recovery step used
// when a factory-reset intent cannot be committed because NVS is full.
inline bool releaseNvsSpaceForFactoryReset(ShotLog &shotLog,
                                           LastShotStore &lastShot) {
  return shotLog.erasePersisted() && lastShot.erasePersisted();
}

// Erase independent NVS history first, then
// overwrite dual-slot settings without clearing the shared NVS namespace,
// BLE companion last. Every store is verified before success. Idempotent
// except that a mid-fail may already have dropped history.
// Orchestrator last-shot UI snapshot (`persistedLastShot`) is not this
// store: callers that publish status must drop it after this returns true.
inline bool resetAllDurableStores(PersistedSettings &settings,
                                  BleCompanionPersistedSettings &ble,
                                  ShotLog &shotLog,
                                  LastShotStore &lastShot,
                                  ShotCurveLog &shotCurves) {
  yieldFlashIo();
  feedFlashIoWatchdog();
  // Drop history blobs first so the NVS partition has room for
  // factory settings writes. A later failure may already have erased
  // history; settings stay until resetPersistedSettingsToFactory succeeds.
  if (!releaseNvsSpaceForFactoryReset(shotLog, lastShot)) {
    return false;
  }
  yieldFlashIo();
  feedFlashIoWatchdog();
  if (!shotLog.clear() || !shotCurves.clear() || !lastShot.clear()) {
    return false;
  }
  yieldFlashIo();
  feedFlashIoWatchdog();
  if (!resetPersistedSettingsToFactory(settings)) {
    return false;
  }
  yieldFlashIo();
  feedFlashIoWatchdog();
  if (!resetBleCompanionSettings(ble)) {
    return false;
  }

  yieldFlashIo();
  feedFlashIoWatchdog();
  PersistedSettings verifiedSettings;
  BleCompanionPersistedSettings verifiedBle;
  const bool shotLogVerified = shotLog.load() && shotLog.count() == 0;
  const bool shotCurvesVerified = shotCurves.load() && shotCurves.count() == 0;
  const bool lastShotVerified = lastShot.load() && !lastShot.get().valid &&
                                !lastShot.getGood().valid;
  return loadPersistedSettings(verifiedSettings) &&
         verifyFactorySettings(verifiedSettings) &&
         loadBleCompanionSettings(verifiedBle) && verifiedBle.enabled == 0 &&
         verifiedBle.scanIntensity ==
             static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE) &&
         shotLogVerified && shotCurvesVerified && lastShotVerified;
}

}  // namespace shotstopper
