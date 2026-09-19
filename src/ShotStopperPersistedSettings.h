#pragma once

#include "ShotStopperDomain.h"
#include "ShotStopperBullseye.h"
#include "ShotStopperPresets.h"
#include "ShotStopperWebhook.h"

namespace shotstopper {

constexpr uint32_t PERSISTED_SETTINGS_MAGIC = 0x53544F50U;  // "STOP"
constexpr const char *SETTINGS_NAMESPACE = "shotstopper";
constexpr const char *SETTINGS_SLOT_A = "settingsA";
constexpr const char *SETTINGS_SLOT_B = "settingsB";
constexpr const char *DEFAULT_DEVICE_PASSWORD = "ineedacoffee";
constexpr size_t MICRA_TOKEN_CAPACITY = 65;
constexpr size_t MICRA_IDENTITY_CAPACITY = 24;
constexpr uint8_t MACHINE_INTEGRATION_APPLY_TEMPERATURE = 1U << 0;
constexpr uint8_t MACHINE_INTEGRATION_OBSERVE_STATE = 1U << 1;

struct MachineIntegrationPersistedSettings {
  char token[MICRA_TOKEN_CAPACITY] = {};
  uint8_t peerAddress[6] = {};
  char identity[MICRA_IDENTITY_CAPACITY] = {};
  uint8_t peerAddressType = 0;
  uint8_t options = 0;
  bool bindingVerified = false;
};

inline bool validMachineIntegrationSettings(
    const MachineIntegrationPersistedSettings &settings) {
  const size_t tokenLength = strnlen(settings.token, sizeof(settings.token));
  if (tokenLength != 0 && tokenLength != 64) return false;
  for (size_t i = 0; i < tokenLength; ++i) {
    const uint8_t value = static_cast<uint8_t>(settings.token[i]);
    if (value < 0x20 || value > 0x7e) return false;
  }
  if ((settings.options & ~(MACHINE_INTEGRATION_APPLY_TEMPERATURE |
                            MACHINE_INTEGRATION_OBSERVE_STATE)) != 0) {
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
  if (tokenLength != 64 || identityLength == 0) {
    return false;
  }
  bool nonzeroAddress = false;
  for (uint8_t value : settings.peerAddress) nonzeroAddress |= value != 0;
  return nonzeroAddress;
}

static_assert(sizeof(MachineIntegrationPersistedSettings) <= 112,
              "machine integration record must stay compact");

struct PersistedSettings {
  uint32_t magic = PERSISTED_SETTINGS_MAGIC;
  uint32_t schemaVersion = CONFIG_SCHEMA_VERSION;
  uint32_t structureSize = 0;
  uint32_t storageRevision = 0;
  RuntimeConfig runtime = {};
  BullseyeMelodyConfig bullseyeMelody = {};
  ShotPresetBank presets = {};
  bool staConfigured = false;
  bool staOpen = false;
  bool staWifiSleep = true;
  char staSsid[WIFI_SSID_CAPACITY] = {};
  char staPassword[WIFI_PASSWORD_CAPACITY] = {};
  uint8_t staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  uint8_t staIp[4] = {};
  uint8_t staNetmask[4] = {};
  uint8_t staGateway[4] = {};
  uint8_t staDns1[4] = {};
  uint8_t staDns2[4] = {};
  uint8_t staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  bool lkgValid = false;
  bool lkgOpen = false;
  char lkgSsid[WIFI_SSID_CAPACITY] = {};
  char lkgPassword[WIFI_PASSWORD_CAPACITY] = {};
  uint8_t lkgIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  uint8_t lkgIp[4] = {};
  uint8_t lkgNetmask[4] = {};
  uint8_t lkgGateway[4] = {};
  uint8_t lkgDns1[4] = {};
  uint8_t lkgDns2[4] = {};
  char devicePassword[WIFI_PASSWORD_CAPACITY] = {};
  char preferredScaleMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  char preferredScaleName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  ScaleHistoryEntry scaleHistory[SCALE_HISTORY_CAPACITY] = {};
  WebhookConfig webhook = {};
  // V14: mDNS device name. Must stay immediately before checksum so V6–V13
  // blobs remain a layout-compatible prefix (see ShotStopperSettingsMigrate.h).
  char deviceName[DEVICE_NAME_CAPACITY] = {};
  // V15: optional Micra credential, verified peer and independent options.
  MachineIntegrationPersistedSettings machineIntegration = {};
  uint32_t checksum = 0;
};

struct SettingsPersistRequest {
  PersistedSettings blob;
  uint32_t runtimeRevision = 0;
};

static_assert(sizeof(SettingsPersistRequest) <=
                  PERSISTED_SETTINGS_NVS_BUDGET + 16,
              "SettingsPersistRequest must stay a single NVS blob plus revision");

struct PersistedSettingsHeader {
  uint32_t magic = 0;
  uint32_t schemaVersion = 0;
  uint32_t structureSize = 0;
  uint32_t storageRevision = 0;
};

static_assert(offsetof(PersistedSettings, storageRevision) + sizeof(uint32_t) ==
                  sizeof(PersistedSettingsHeader),
              "PersistedSettings header layout changed");

static_assert(sizeof(PersistedSettings) <= PERSISTED_SETTINGS_NVS_BUDGET,
              "PersistedSettings exceeds NVS dual-slot budget");
static_assert(sizeof(PersistedSettings) == 2748,
              "PersistedSettings size changed; bump CONFIG_SCHEMA_VERSION");

inline uint32_t persistedSettingsChecksum(const PersistedSettings &settings) {
  return crc32(reinterpret_cast<const uint8_t *>(&settings),
               offsetof(PersistedSettings, checksum));
}

}  // namespace shotstopper
