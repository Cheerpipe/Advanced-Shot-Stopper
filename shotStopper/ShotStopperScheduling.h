#pragma once

#include <stddef.h>
#include <stdint.h>

namespace shotstopper {

// Qualification contract for the two hard real-time-adjacent application
// loops. The nominal cadence is 1 ms while a scale is active; a 10 ms service
// deadline leaves radio/flash jitter headroom while remaining far below every
// control safety timeout. Deadline misses are monotonic and exported in the
// coherent ControlStatus snapshot.
constexpr uint32_t CONTROL_SERVICE_DEADLINE_MS = 10;
constexpr uint32_t SCALE_SERVICE_DEADLINE_MS = 10;

struct TaskScheduleContract {
  const char *name;
  int8_t core;
  uint8_t priorityOffset;
  uint32_t nominalPeriodMs;
  uint32_t serviceDeadlineMs;
  uint32_t watchdogDeadlineMs;
  bool watchdogSubscribed;
};

constexpr TaskScheduleContract TASK_SCHEDULE_CONTRACTS[] = {
    {"control", 1, 1, 1, CONTROL_SERVICE_DEADLINE_MS, 5000, true},
    {"scale_worker", 1, 1, 1, SCALE_SERVICE_DEADLINE_MS, 5000, true},
    {"settings_persist", 1, 1, 0, 1000, 5000, true},
    {"network_manager", 0, 1, 50, 250, 5000, true},
    {"httpd", 0, 1, 0, 0, 0, false},
    {"webhook", 0, 0, 0, 0, 0, false},
    {"serial_log", 0, 0, 0, 0, 0, false},
};

constexpr size_t TASK_SCHEDULE_CONTRACT_COUNT =
    sizeof(TASK_SCHEDULE_CONTRACTS) / sizeof(TASK_SCHEDULE_CONTRACTS[0]);

static_assert(CONTROL_SERVICE_DEADLINE_MS < 200,
              "control deadline must precede health warning threshold");
static_assert(SCALE_SERVICE_DEADLINE_MS < 250,
              "scale deadline must precede stale-link detection");

}  // namespace shotstopper
