#pragma once

// NVS I/O for the Web UI access blob (OpenBrewByWeightWebAccess.h). One key in
// the settings namespace; NVS keeps the previous value until a write commits.
// Callers pass a stack copy because flash I/O must not read PSRAM.

#include "OpenBrewByWeightPersistence.h"
#include "OpenBrewByWeightWebAccess.h"

namespace openbrewbyweight {

constexpr const char *WEB_ACCESS_KEY = "webAccess";

#if !defined(OPEN_BREW_BY_WEIGHT_HOST_TEST) ||                                      \
    defined(OPEN_BREW_BY_WEIGHT_PERSISTENCE_HOST_TEST)

enum class WebAccessLoad : uint8_t { LOADED, MISSING, INVALID };

// A missing or invalid blob loads as the default: login off. INVALID (a
// record that cannot be read or fails its checksum) is worth a log line. A
// version 0 record (before device labels) is migrated; the next save rewrites it.
inline WebAccessLoad loadWebAccessStored(WebAccessStored &stored) {
  WebAccessLoad result = WebAccessLoad::INVALID;
  if (lockSettingsNvs()) {
    OpenBrewByWeightPreferences preferences(NvsSubsystem::WEB_ACCESS);
    // An absent namespace is a fresh device; NVS logs real open failures.
    result = WebAccessLoad::MISSING;
    if (preferences.begin(SETTINGS_NAMESPACE, true)) {
      if (preferences.isKey(WEB_ACCESS_KEY)) {
        const size_t length = preferences.getBytesLength(WEB_ACCESS_KEY);
        WebAccessStoredV0 old;
        const bool loaded =
            length == sizeof(stored)
                ? preferences.getBytes(WEB_ACCESS_KEY, &stored, sizeof(stored)) ==
                          sizeof(stored) &&
                      validWebAccessStored(stored)
                : length == sizeof(old) &&
                      preferences.getBytes(WEB_ACCESS_KEY, &old, sizeof(old)) == sizeof(old) &&
                      migrateWebAccessV0(old, stored);
        memset(&old, 0, sizeof(old));
        result = loaded ? WebAccessLoad::LOADED : WebAccessLoad::INVALID;
      }
      preferences.end();
    }
    unlockSettingsNvs();
  }
  if (result != WebAccessLoad::LOADED) stored = WebAccessStored{};
  return result;
}

inline bool saveWebAccessStored(WebAccessStored &stored) {
  finalizeWebAccessStored(stored);
  if (!lockSettingsNvs()) return false;
  OpenBrewByWeightPreferences preferences(NvsSubsystem::WEB_ACCESS);
  bool saved = false;
  if (preferences.begin(SETTINGS_NAMESPACE, false)) {
    saved = preferences.putBytes(WEB_ACCESS_KEY, &stored, sizeof(stored)) ==
            sizeof(stored);
    preferences.end();
  }
  unlockSettingsNvs();
  return saved;
}

// Factory reset forgets everything, including the Home notice choice.
inline bool eraseWebAccessStored() {
  if (!lockSettingsNvs()) return false;
  OpenBrewByWeightPreferences preferences(NvsSubsystem::WEB_ACCESS);
  bool erased = false;
  if (preferences.begin(SETTINGS_NAMESPACE, false)) {
    erased = !preferences.isKey(WEB_ACCESS_KEY) || preferences.remove(WEB_ACCESS_KEY);
    preferences.end();
  }
  unlockSettingsNvs();
  return erased;
}
#endif

}  // namespace openbrewbyweight
