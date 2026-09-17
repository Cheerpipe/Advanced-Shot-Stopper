#pragma once

#include "ShotStopperBleScanPersistence.h"
#include "ShotStopperHistory.h"
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
  if (!loadPersistedSettings(candidate) || candidate.staConfigured ||
      candidate.lkgValid ||
      candidate.staIpMode != static_cast<uint8_t>(StaIpMode::DHCP) ||
      !passwordIsFactoryDefault(candidate)) {
    return false;
  }
  settings = candidate;
  return true;
}

// Erase only expendable NVS records. This is the narrow recovery step used
// when a factory-reset intent cannot be committed because NVS is full. The
// shot log and activation history live in their own flash partitions and are
// cleared by resetAllDurableStores below.
inline bool releaseNvsSpaceForFactoryReset(LastShotStore &lastShot) {
  return lastShot.erasePersisted();
}

// Erase the flash-backed activation stores first, then
// overwrite dual-slot settings without clearing the shared NVS namespace,
// BLE scan settings last. Every store is verified before success. Idempotent
// except that a mid-fail may already have dropped history.
// LastShotStore owns the status aggregate. Callers only need to drop transient
// dirty state after this durable reset succeeds.
inline bool resetAllDurableStores(PersistedSettings &settings,
                                  BleScanPersistedSettings &ble,
                                  ShotLog &shotLog,
                                  HistoryLog &historyLog,
                                  LastShotStore &lastShot,
                                  ShotCurveLog &shotCurves) {
  yieldFlashIo();
  feedFlashIoWatchdog();
  // Drop the last-shot NVS record first so the NVS partition has room for
  // factory settings writes. A later failure may already have erased it;
  // settings stay until resetPersistedSettingsToFactory succeeds.
  if (!releaseNvsSpaceForFactoryReset(lastShot)) {
    return false;
  }
  yieldFlashIo();
  feedFlashIoWatchdog();
  if (!shotLog.clear() || !historyLog.clear() || !shotCurves.clear() ||
      !lastShot.clear()) {
    return false;
  }
  yieldFlashIo();
  feedFlashIoWatchdog();
  if (!resetPersistedSettingsToFactory(settings)) {
    return false;
  }
  yieldFlashIo();
  feedFlashIoWatchdog();
  if (!resetBleScanSettings(ble)) {
    return false;
  }

  yieldFlashIo();
  feedFlashIoWatchdog();
  PersistedSettings verifiedSettings;
  BleScanPersistedSettings verifiedBle;
  const bool shotLogVerified = shotLog.load() && shotLog.count() == 0;
  const bool historyVerified = historyLog.load() && historyLog.count() == 0;
  const bool shotCurvesVerified = shotCurves.load() && shotCurves.count() == 0;
  const bool lastShotVerified = lastShot.load() && !lastShot.get().valid &&
                                !lastShot.getGood().valid;
  return loadPersistedSettings(verifiedSettings) &&
         verifyFactorySettings(verifiedSettings) &&
         readLatestBleScanSettings(verifiedBle) &&
         verifyFactoryBleScanSettings(verifiedBle) &&
         shotLogVerified && historyVerified && shotCurvesVerified &&
         lastShotVerified;
}

}  // namespace shotstopper
