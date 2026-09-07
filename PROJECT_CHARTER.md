# Project charter

Advanced Shot Stopper is ESP32-S3 firmware that adds local brew-by-weight to
espresso machines while preserving their physical human interface. It supports
n8r4 and n16r8 ESP32-S3 boards with PSRAM, latch/paddle machines, momentary
machines, and momentary machines with a reed or hall sensor. EspressoScaleBLE is
the local BLE scale library.

The project supplies firmware and wiring guidance, not a certified or
plug-and-play appliance. Legacy ESP32 targets, boards without PSRAM, MQTT, persistent remote-control
integrations, and guaranteed Web UI live telemetry are not project goals.
Remote shot start/rinse are disabled in default firmware; a deliberate
compile-time development opt-in exists but is not the default user workflow.

## Safety boundary

The relay is the only connection to the machine activation circuit and must be
open on startup, reset, power loss, failed boot readiness, and any safety fault.
Physical intent remains authoritative; remote activation is disabled by default.
Hard timing limits, watchdog coverage, image verification, rollback, memory
budgets, concurrency rules, and resource ownership are release constraints.

No automated result substitutes for bench wiring inspection or required HIL and
manual evidence. See `VALIDATION.md`, `docs/HARDWARE.md`, and
`docs/MANUAL_TEST_PLAN.md` before connecting a machine.
