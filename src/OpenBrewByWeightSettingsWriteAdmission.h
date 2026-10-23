#pragma once

#include <atomic>

namespace openbrewbyweight {

// Boot reads the image state before any settings/reset writer. OTA confirmation
// releases this latch; only settings writes are deferred, not other stores.
inline std::atomic<bool> settingsSchemaWritesAdmitted{
#if defined(OPEN_BREW_BY_WEIGHT_HOST_TEST) || defined(OPEN_BREW_BY_WEIGHT_PERSISTENCE_HOST_TEST) || defined(OPEN_BREW_BY_WEIGHT_OTA_HOST_TEST)
    true
#else
    false
#endif
};

// The network task sets this after committing a Settings-page change to the
// PSRAM record; the control task turns it into the deferred settings persist.
inline std::atomic<bool> networkSettingsPersistRequested{false};

}  // namespace openbrewbyweight
