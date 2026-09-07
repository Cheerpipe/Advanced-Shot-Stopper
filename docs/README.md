# Documentation map

Use this index after `AGENTS.md`. Each row maps a need to the smallest useful
documentation, editable sources, focused tests, and canonical command. Run
`./scripts/dev context <area>` for the same map without loading file contents.

| Need | Documentation | Editable sources | Tests | Command |
| --- | --- | --- | --- | --- |
| Safety and relay | `PROJECT_CHARTER.md`, `HARDWARE.md`, `MANUAL_TEST_PLAN.md` | `src/ShotStopperSafety.h`, `src/ShotStopperMachineRelay.h`, `src/ShotStopperHardware*` | `safety_external_host_test.cpp`, main host scenarios | `./scripts/dev validate --risk R3` |
| Control/brew | `ARCHITECTURE.md`, `STATE_MACHINES.md`, feature docs | `src/ShotStopperBrew*`, `src/ShotStopperDomain.h` | `shot_stopper_host_test.cpp` | `./scripts/dev test normal` |
| Machine inputs | `HARDWARE.md`, `settings/paddle.md`, `settings/momentary.md` | `src/ShotStopperMachine*` | `momentary_machine_host_test.cpp`, main host tests | `./scripts/dev validate --risk R3` |
| Scale sensing | `ARCHITECTURE.md`, `CONCURRENCY.md` | `src/ShotStopperScale*` | main host tests | `./scripts/dev test normal` |
| BLE protocols | library README, `CONCURRENCY.md` | `libraries/EspressoScaleBLE/src/`, `src/ble/` | library tests, `ble_companion_protocol_host_test.cpp` | `./scripts/dev test ble` |
| Network/Webhooks | feature docs, `RESOURCE_OWNERSHIP.md` | `src/ShotStopperNetwork*`, `src/ShotStopperWebhook*` | `webhook_error_host_test.cpp`, Web contract tests | `./scripts/dev validate --risk R2` |
| OTA | `features/ota.md`, `features/ota-image-identity.md`, `EMERGENCY_RECOVERY.md` | `src/ShotStopperOta*`, `scripts/shotstopper_ota.sh` | OTA host, CLI, and Web resilience tests | `./scripts/dev test ota` |
| Persistence | `ARCHITECTURE.md`, settings docs | `src/ShotStopperPersist*`, durable stores/logs | `persistence_host_test.cpp` | `./scripts/dev test normal` |
| Web UI | `SCREENSHOTS.md`, feature/settings docs | `src/web/` | Web asset/contract tests | `./scripts/dev test web` |
| Build and partitions | `BUILD.md`, `SCRIPTS.md`, `STATIC_ANALYSIS.md` | `idf/`, build scripts | architecture and size checks | `./scripts/dev build --arch n8r4` |
| Scheduling/resources | `SCHEDULABILITY.md`, `P2_RESOURCE_BUDGETS.md`, `P2_TARGET_TRACE.md` | task/owner/watchdog headers | TSAN and resource-owner tests | `./scripts/dev test tsan` |
| Developer tooling | `AI_WORKFLOW.md`, root `VALIDATION.md` | `scripts/`, `.github/` | dev contract and risk golden tests | `./scripts/dev test tooling` |

## Repository boundaries

- Canonical firmware: `src/`; Web source: `src/web/`; BLE library:
  `libraries/EspressoScaleBLE/`; ESP-IDF integration: `idf/`.
- Generated: `src/ShotStopperVersion.h`,
  `src/ShotStopperWebAssetsGzip.h`, build trees, reports, and `artifacts/`.
- Vendored/dependency output: `idf/managed_components/` and `node_modules/`.
  Do not review or load these trees unless dependency provenance itself is the
  task. Lockfiles and IDF configuration remain visible and reviewable.
- Current safety authority is the charter, validation policy, architecture
  documents, and manual test plan.

## Key policies

- [Project charter](../PROJECT_CHARTER.md)
- [Validation gates](../VALIDATION.md)
- [Contribution workflow](../CONTRIBUTING.md)
- [AI workflow](AI_WORKFLOW.md)
- [Build](BUILD.md) and [script reference](SCRIPTS.md)
- [Manual test plan](MANUAL_TEST_PLAN.md)
