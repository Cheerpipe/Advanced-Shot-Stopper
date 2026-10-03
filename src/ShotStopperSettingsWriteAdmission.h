#pragma once

#include <atomic>

namespace shotstopper {

// Boot reads the image state before any settings/reset writer. OTA confirmation
// releases this latch; only settings writes are deferred, not other stores.
inline std::atomic<bool> settingsSchemaWritesAdmitted{
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST) || defined(SHOT_STOPPER_OTA_HOST_TEST)
    true
#else
    false
#endif
};

}  // namespace shotstopper
