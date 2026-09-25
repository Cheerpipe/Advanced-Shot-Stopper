#pragma once

#include <stdint.h>

namespace shotstopper {

enum class PowerProfile : uint8_t {
  OFF, IDLE, WAITING, WORKING, MANUAL, COOLDOWN, WEB, MAINTENANCE, SETTLING
};

inline const char *powerProfileName(PowerProfile profile) {
  static const char *const names[] = {
      "off", "idle", "waiting", "working", "manual", "cooldown", "web",
      "maintenance", "settling"};
  return names[static_cast<uint8_t>(profile)];
}

inline int powerMinMhz(PowerProfile profile) {
  return profile == PowerProfile::IDLE ? 40
         : profile == PowerProfile::WORKING || profile == PowerProfile::WAITING
             ? 160 : 80;
}
inline int powerMaxMhz(PowerProfile profile) {
  return profile == PowerProfile::WORKING || profile == PowerProfile::WAITING ? 160 : 80;
}

struct PowerInputs {
  bool enabled = false;
  bool machineBusy = false;  // Includes uncertain stop and start/stop pulses.
  bool scaleBusy = false;    // Includes GAP/GATT setup, before usable weight.
  bool physicalEdge = false;
  bool webActive = false;
  bool maintenance = false;
};

// Control owner only. Timers govern energy, never actuation or shot duration.
class PowerPolicy {
 public:
  static constexpr uint32_t cooldownMs = 300000;
  static constexpr uint32_t scaleGraceMs = 30000;
  PowerProfile update(uint32_t now, const PowerInputs &in) {
    if (in.physicalEdge || (wasBusy_ && !in.machineBusy)) {
      lastUseMs_ = now;
      cooldown_ = true;
    }
    wasBusy_ = in.machineBusy;
    if (wasScaleBusy_ && !in.scaleBusy) {
      scaleDisconnectedMs_ = now;
      scaleGrace_ = true;
    }
    wasScaleBusy_ = in.scaleBusy;
    if (scaleGrace_ && uint32_t(now - scaleDisconnectedMs_) >= scaleGraceMs)
      scaleGrace_ = false;
    const bool scaleBoost = in.scaleBusy || scaleGrace_;
    if (cooldown_ && uint32_t(now - lastUseMs_) >= cooldownMs)
      cooldown_ = false;
    PowerProfile desired = PowerProfile::IDLE;
    if (!in.enabled) desired = PowerProfile::OFF;
    else if (in.machineBusy)
      desired = scaleBoost ? PowerProfile::WORKING : PowerProfile::MANUAL;
    else if (scaleBoost) desired = PowerProfile::WAITING;
    else if (in.maintenance) desired = PowerProfile::MAINTENANCE;
    else if (in.webActive) desired = PowerProfile::WEB;
    else if (cooldown_) desired = PowerProfile::COOLDOWN;
    if (desired != PowerProfile::IDLE) idlePending_ = false;
    else {
      if (!idlePending_) {
        idlePending_ = true;
        idleSinceMs_ = now;
      }
      if (uint32_t(now - idleSinceMs_) < 1000)
        desired = PowerProfile::SETTLING;
    }
    return desired;
  }
  uint32_t cooldownRemaining(uint32_t now) const {
    const uint32_t elapsed = now - lastUseMs_;
    return cooldown_ && elapsed < cooldownMs ? cooldownMs - elapsed : 0;
  }

 private:
  uint32_t lastUseMs_ = 0;
  uint32_t scaleDisconnectedMs_ = 0;
  uint32_t idleSinceMs_ = 0;
  bool cooldown_ = false;
  bool wasBusy_ = false;
  bool wasScaleBusy_ = false;
  bool scaleGrace_ = false;
  bool idlePending_ = false;
};

// A failed boost degrades to legacy 80 MHz. Never retry a fault every tick.
class PowerClock {
 public:
  template <typename Configure>
  bool apply(PowerProfile requested, Configure configure) {
    if (failed_) requested = PowerProfile::OFF;
    if (initialized_ && powerMinMhz(requested) == powerMinMhz(applied_) &&
        powerMaxMhz(requested) == powerMaxMhz(applied_)) {
      applied_ = requested;
      return safe_;
    }
    error_ = configure(powerMinMhz(requested), powerMaxMhz(requested));
    safe_ = error_ == 0;
    if (!safe_) {
      failed_ = true;
      requested = PowerProfile::OFF;
      safe_ = configure(80, 80) == 0;
    }
    initialized_ = true;
    applied_ = requested;
    return safe_;
  }
  PowerProfile applied() const { return applied_; }
  int error() const { return error_; }

 private:
  PowerProfile applied_ = PowerProfile::OFF;
  int error_ = 0;
  bool initialized_ = false;
  bool failed_ = false;
  bool safe_ = false;
};

}  // namespace shotstopper
