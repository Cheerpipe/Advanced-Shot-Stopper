#pragma once

#include <cstdint>

namespace shotstopper {

enum class MachineBackflushPhase : uint8_t {
  UNSUPPORTED, UNKNOWN, INACTIVE, AWAITING, ACTIVE
};

struct MachineBackflushPermit {
  uint32_t continuity = 0, episode = 0, attempt = 0, sequence = 0;
  bool extendable = false;
};

struct MachineBackflushChange {
  MachineBackflushPermit permit;
  uint32_t sequence = 0;
  MachineBackflushPhase phase = MachineBackflushPhase::UNKNOWN;
};

struct MachineBackflushSnapshot {
  static constexpr uint8_t capacity = 8;
  MachineBackflushChange changes[capacity] = {};
  MachineBackflushPermit permit;
  uint32_t sequence = 0;
  uint8_t count = 0;
  MachineBackflushPhase phase = MachineBackflushPhase::UNSUPPORTED;
  bool valid = false, unresolved = false, consumed = false, ready = false;

  bool busy() const {
    return unresolved || phase == MachineBackflushPhase::AWAITING ||
           phase == MachineBackflushPhase::ACTIVE || permit.attempt != 0;
  }
};

// Integration-owned bounded handoff. Only ingressAttempt() is callback-safe
// without the integration mutex. Control alone drains; loss survives recovery.
class MachineBackflushObservations {
 public:
  const MachineBackflushSnapshot &status() const { return state_; }
  uint32_t ingressAttempt() const {
    return __atomic_load_n(&attemptCounter_, __ATOMIC_ACQUIRE);
  }

  void invalidate() {
    if (state_.phase == MachineBackflushPhase::UNSUPPORTED) return;
    if (state_.valid || state_.count || state_.permit.extendable) {
      advance(state_.permit.continuity);
      state_.count = 0;
    }
    state_.valid = false;
    state_.permit.extendable = false;
    state_.phase = MachineBackflushPhase::UNKNOWN;
    inactiveSeen_ = false;
  }

  void observe(MachineBackflushPhase phase, uint32_t ingressAttempt) {
    if (state_.phase == MachineBackflushPhase::UNSUPPORTED) {
      state_.phase = MachineBackflushPhase::UNKNOWN;
      advance(state_.permit.continuity);
    }
    if (phase == MachineBackflushPhase::UNKNOWN) {
      state_.unresolved = true;
      invalidate();
      return;
    }
    if (state_.valid && phase == state_.phase) return;
    if (state_.count == MachineBackflushSnapshot::capacity) {
      invalidate();
      state_.unresolved = true;
      return;
    }
    if (phase == MachineBackflushPhase::AWAITING) {
      advance(state_.permit.episode);
      state_.permit.extendable = inactiveSeen_ &&
          state_.phase == MachineBackflushPhase::INACTIVE;
    }
    if (phase == MachineBackflushPhase::INACTIVE) {
      inactiveSeen_ = true;
      state_.unresolved = false;
      state_.consumed = false;
    } else {
      state_.unresolved = true;
    }
    state_.valid = true;
    state_.phase = phase;
    advance(state_.sequence);
    auto &change = state_.changes[state_.count++];
    change = {state_.permit, state_.sequence, phase};
    change.permit.attempt = ingressAttempt;
  }

  // Every evaluated edge fences bytes already in flight, even a normal start.
  MachineBackflushPermit start() {
    advanceAttempt();
    if (!state_.valid || state_.phase != MachineBackflushPhase::AWAITING ||
        state_.consumed) return {};
    state_.consumed = true;
    state_.permit.attempt = ingressAttempt();
    state_.permit.sequence = state_.sequence;
    state_.count = 0;
    return state_.permit;
  }

  void finish(uint32_t attempt) {
    if (state_.permit.attempt == attempt) state_.permit.attempt = 0;
    advanceAttempt();
  }

  MachineBackflushSnapshot take() {
    const auto result = state_;
    state_.count = 0;
    return result;
  }

 private:
  static void advance(uint32_t &value) { if (++value == 0) ++value; }
  void advanceAttempt() {
    if (__atomic_add_fetch(&attemptCounter_, 1U, __ATOMIC_RELEASE) == 0)
      (void)__atomic_add_fetch(&attemptCounter_, 1U, __ATOMIC_RELEASE);
  }
  MachineBackflushSnapshot state_;
  uint32_t attemptCounter_ = 0;
  bool inactiveSeen_ = false;
};

}  // namespace shotstopper
