#pragma once

#include "ShotStopperDomain.h"

namespace shotstopper {

struct RuntimeSettingsSubscription {
  bool (*changed)(const RuntimeConfig &, const RuntimeConfig &);
  void (*notify)(const RuntimeConfig &, const RuntimeConfig &);
};

template <size_t N>
inline void dispatchRuntimeSettingsChanges(
    const RuntimeConfig &before, const RuntimeConfig &after,
    const RuntimeSettingsSubscription (&subscribers)[N]) {
  for (const auto &subscriber : subscribers)
    if (subscriber.changed(before, after)) subscriber.notify(before, after);
}

}  // namespace shotstopper
