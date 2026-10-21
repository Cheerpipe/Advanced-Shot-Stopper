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

}  // namespace openbrewbyweight
