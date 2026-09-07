# Validation gates

Classify changed paths with `./scripts/dev classify [paths...]`. Unknown paths
are R3 and an override can only raise risk. Complete logs and a redacted JSON
summary are written to `artifacts/runs/`.

| Risk | Typical change | Required automated gate | Manual evidence |
| --- | --- | --- | --- |
| R0 | Non-critical docs/meta | contract, link, and path checks | none |
| R1 | Web UI, tests, tooling, pure logic | R0 + focused tests and generated assets | as identified |
| R2 | BLE, network, persistence, OTA, build | full host + ASan/UBSan + TSAN + architecture + n8r4/n16r8 builds | subsystem-dependent |
| R3 | Relay, machine, ISR, watchdog, boot, GPIO, partitions, remote control, unknown | R2 + warnings/cppcheck + build variants | HIL/manual required |
| Release | Candidate firmware image | complete analysis, resource budgets, applicable soak, HIL and manual plan | required |

Use `./scripts/dev validate [--risk R0..R3] [paths...]`. Focused commands are
`./scripts/dev test [normal|asan|tsan|web|ble]`, `./scripts/dev build`, and
`./scripts/dev analyze`. The supported IDF setup and commands remain documented
in [Build](docs/BUILD.md), [scripts](docs/SCRIPTS.md), and
[static analysis](docs/STATIC_ANALYSIS.md). Hardware acceptance is defined by
the [manual test plan](docs/MANUAL_TEST_PLAN.md) and curated evidence by
[target trace qualification](docs/P2_TARGET_TRACE.md).

Missing tools or dependencies fail profiles that require them with exit 127.
Tests never bootstrap packages or access hardware/network implicitly. R3 is not
release-ready while required HIL/manual evidence is pending. Image and memory
regions are compared by `src/tests/check_firmware_size.js` against the versioned
budgets in `docs/P2_RESOURCE_BUDGETS.md`.
