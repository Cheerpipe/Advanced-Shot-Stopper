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
`./scripts/dev test [normal|asan|tsan|web|ble|ota|tooling]`, `./scripts/dev build`, and
`./scripts/dev analyze`. The supported IDF setup and commands remain documented
in [Build](docs/BUILD.md), [scripts](docs/SCRIPTS.md), and
[static analysis](docs/STATIC_ANALYSIS.md). Hardware acceptance is defined by
the [manual test plan](docs/MANUAL_TEST_PLAN.md) and curated evidence by
[target trace qualification](docs/P2_TARGET_TRACE.md).

Any change to `src/ShotStopperNetwork.cpp`,
`src/network/ShotStopperHttpLifecycle.inc`, or another source that defines or
registers HTTP routes or handlers must run `./scripts/dev test web`. That profile,
and every R2/R3 validation gate, runs the named `http-route-capacity` check before
the broader Web contracts. The check counts all `registerHandler(server_, ...)`
registrations in the network sources and requires `max_uri_handlers` to be
strictly greater, preserving at least one spare slot.

Missing tools or dependencies fail profiles that require them with exit 127.
Tests never bootstrap packages or access hardware/network implicitly. R3 is not
release-ready while required HIL/manual evidence is pending. Image and memory
regions are compared by `src/tests/check_firmware_size.js` against the versioned
budgets in `docs/P2_RESOURCE_BUDGETS.md`.

The documentation check covers root guides, canonical guides in `docs/`, the BLE
library README, and the safety README. Local working files in `temp/`,
`docs/plans/`, and `docs/audits/` are not documentation sources for this check.
They remain part of the local project but outside Git; validate their evidence
and progress through the [AI workflow](docs/AI_WORKFLOW.md#local-project-files-and-git).
The check validates local paths, images, and Markdown heading/
explicit HTML anchors (including duplicate headings and multiline links).
External URLs and example contents are not certified by this offline check.

## Reading a result

A zero exit code means the automated profile passed, not that hardware is
qualified. Use the printed `artifacts/runs/<run-id>/summary.json` and full log
to inspect which steps ran. Exit 127 means a required dependency is missing;
record the gate as failed and prepare the dependency explicitly.

GitHub Actions publishes bounded-retention artifacts even when a validation
command fails. The `validation-classify`, `validation-fast`, and
`validation-host` archives contain the available console logs and `scripts/dev`
run records. Each of the six `shotstopper-ota-<arch>-<machine>-jtag-off-remote-off`
archives contains its firmware binary when the build succeeds, plus the
available IDF command logs, static-analysis reports, and run records. Only steps
that started can produce diagnostics; a failed prerequisite may leave later
entries absent.

Classify concrete files, not a directory name such as `docs`. Safety-related
documents and BLE/OTA references can select R3/R2 even though they are Markdown.
Do not lower risk to avoid an unavailable tool or missing physical evidence.
