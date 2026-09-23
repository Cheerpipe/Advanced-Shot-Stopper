# Validation gates

Classify changed paths with `./scripts/dev classify [paths...]`. Unknown paths
are R3 and an override can only raise risk. Complete logs and a redacted JSON
summary are written to `artifacts/runs/`.

When several agents share this checkout, always pass the paths of your change
and read the `warning: changed path outside requested scope` lines: they name
other sessions' working-tree edits that your gate did not cover, so a clean
gate is never mistaken for a clean tree. Omitting paths classifies the whole
working tree and prints no warning.

Every change — including documentation-only edits — must be reviewed and
validated rigorously: run **all** gates that apply to the change's classified
risk, review the complete diff against the affected invariants, and record a
gate that cannot run as failed instead of skipping it silently. Passing a
subset of the applicable gates does not validate the change.

| Risk | Typical change | Required automated gate | Manual evidence |
| --- | --- | --- | --- |
| R0 | Non-critical docs/meta | contract, link, and path checks | none |
| R1 | Web UI, tests, tooling, pure logic | R0 + focused tests and generated assets | as identified |
| R2 | BLE, network, persistence, OTA, build | full host + ASan/UBSan + TSAN + architecture + every official validation profile pair | subsystem-dependent |
| R3 | Relay, machine, ISR, watchdog, boot, GPIO, partitions, remote control, unknown | R2 + warnings/cppcheck + build variants | HIL/manual required |
| Release | Candidate firmware image | complete analysis, resource budgets, applicable soak, HIL and manual plan | required |

Use `./scripts/dev validate [--risk R0..R3] [paths...]`. Focused commands are
`./scripts/dev test [normal|asan|tsan|web|ble|ota|tooling]`, `./scripts/dev build`, and
`./scripts/dev analyze`. The supported IDF setup and commands remain documented
in [Build](docs/BUILD.md), [scripts](docs/SCRIPTS.md), and
[static analysis](docs/STATIC_ANALYSIS.md). Hardware acceptance is defined by
the [manual test plan](docs/MANUAL_TEST_PLAN.md) and curated evidence by
[target trace qualification](docs/P2_TARGET_TRACE.md).

Any change to `src/OpenBrewByWeightNetwork.cpp`,
`src/network/OpenBrewByWeightHttpLifecycle.inc`, or another source that defines or
registers HTTP routes or handlers must run `./scripts/dev test web`. That profile,
and every R2/R3 validation gate, runs the named `http-route-capacity` check before
the broader Web contracts. The check counts all `registerHandler(server_, ...)`
registrations in the network sources and requires `max_uri_handlers` to be
strictly greater, preserving at least one spare slot.

Missing tools or dependencies fail profiles that require them with exit 127.
Tests never bootstrap packages or access hardware/network implicitly. R3 is not
release-ready while required HIL/manual evidence is pending.
Every firmware budget measurement must build with the `--development` profile
(admin unlock plus the JTAG console), as defined by
`docs/P2_RESOURCE_BUDGETS.md`; this profile usually produces the largest image.
R2/R3 validation applies it to the three official profile pairs in
`docs/BUILD_PROFILES.md` and leaves those
conservative images in their profile build directories. Image
and memory regions are compared by
`src/tests/check_firmware_size.js` against those versioned budgets.

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
run records. Each `open-brew-by-weight-ota-<profile>-jtag-off-remote-off` archive
contains its firmware binary when the build succeeds, plus the
available IDF command logs, static-analysis reports, and run records. Only steps
that started can produce diagnostics; a failed prerequisite may leave later
entries absent.

Classify concrete files, not a directory name such as `docs`. Safety-related
documents and BLE/OTA references can select R3/R2 even though they are Markdown.
Do not lower risk to avoid an unavailable tool or missing physical evidence.
