# AI development workflow

## Progressive discovery

Start with `AGENTS.md`, `docs/README.md`, and one scoped `AGENTS.md`; keep this
orientation below about 16 KiB. Run `./scripts/dev context <area>` to list the
smallest relevant docs, sources, tests, and command before reading content.
Search symbols with targeted `rg`; inspect callers and tests before expanding to
an adjacent subsystem. Do not bulk-load builds, managed components,
`node_modules`, reports, generated Web assets, binaries, or STL files.

## Work and evidence

Classify paths first and state safety invariants for R2/R3. Prefer existing
boundaries and focused tests. A run record under `artifacts/runs/<run-id>/`
contains the full log and a redacted `summary.json`; `latest.json` points to the
most recent summary. Reuse a recorded result only when its SHA and inputs are
unchanged. Runtime firmware log level is independent of `scripts/dev`
verbosity.

## Temporary working state

Do not place plans, audits, decision logs, handoffs, or other session records in
`docs/`. Keep temporary working notes outside the repository, delete them when
the task is complete, and record durable behavior only in the canonical document
that owns the affected contract.

## Documentation maintenance

Documentation is part of the implementation, not a later task. Apply this guide
when adding, changing, renaming, or removing settings, commands, interfaces,
supported hardware, workflows, or observable behavior. Internal refactors need
documentation only if they change a documented contract or make a reference stale.

### Choose the owner

Read only the matching guides below and the sources needed to verify the change.
Paths in this table are relative to `docs/` unless prefixed with `../`.
Update additional pages only when their own instructions or claims are affected.

| Change | Canonical documentation to update |
| --- | --- |
| Setting, default, range, or UI control | Matching guide under `settings/` or `features/`, found through [the index](README.md). Brew parameters belong to their feature guide; update [presets](features/presets.md) when preset scope or save/load behavior changes. |
| Brewing behavior, guards, outcomes, or alerts | Matching `features/` guide and [alerts](alerts.md) where affected; [state machines](STATE_MACHINES.md) for transitions or outcome contracts. |
| Shell script, flag, environment variable, or local config | [Script reference](SCRIPTS.md); [Build](BUILD.md) if setup/build steps change, [static analysis](STATIC_ANALYSIS.md) if analysis setup changes. Firmware USB commands belong in [Serial CLI](SERIAL_CLI.md), not the shell reference. |
| Validation gate or contributor workflow | Root [validation policy](../VALIDATION.md), [Contributing](../CONTRIBUTING.md), and applicable `AGENTS.md`; this guide only when agent workflow changes. |
| Webhook or external payload | [Home Assistant integration](features/home-assistant-webhooks.md) and its [maintained YAML example](examples/home-assistant-webhooks.yaml). Other interfaces belong in their owning feature or architecture reference. |
| Persistence, reset, or migration | Owning settings/feature guide for user effects; [architecture](ARCHITECTURE.md) for storage contracts and [factory reset](settings/factory-reset.md) if reset behavior changes. |
| BLE support or protocol | [Library README](../libraries/EspressoScaleBLE/README.md); [scale settings](settings/scales.md) for user-visible compatibility or connection behavior. Distinguish implemented support from hardware-tested support. |
| OTA, image identity, or recovery | [OTA](features/ota.md), [image identity](features/ota-image-identity.md), or [recovery](EMERGENCY_RECOVERY.md), according to the affected procedure. |
| Hardware, safety, ownership, or scheduling | [Hardware](HARDWARE.md), [charter](../PROJECT_CHARTER.md), [architecture](ARCHITECTURE.md), [ownership](RESOURCE_OWNERSHIP.md), [concurrency](CONCURRENCY.md), or [scheduling](SCHEDULABILITY.md), as applicable; update [manual acceptance](MANUAL_TEST_PLAN.md) when acceptance steps change. |

Update [first setup](GETTING_STARTED.md) only if onboarding or daily-use steps
change, and [FAQ](FAQ.md) when symptoms or recovery advice change. Keep the root
[README](../README.md) an overview, not a second settings or command reference.
Add a link in [the index](README.md) for a new guide; change screenshots only when
they are misleading, and identify historical captures rather than inventing evidence.

### Verify and write

1. Identify the owner before editing. Check implementation, callers, and relevant
   tests; do not use old prose, screenshots, or external examples as proof.
2. For a **setting**, verify its exact UI label, purpose, units, accepted range,
   default, and global/per-preset scope. State persistence, when changes take
   effect, and important prerequisites or interactions. Check initialization,
   validation, preset seeds, UI visibility, and serialization where applicable;
   distinguish fresh-install defaults from values retained or migrated on upgrade.
3. For a **command**, verify flags and behavior against its parser/help and
   implementation. Give prerequisites, working directory, a minimal invocation,
   expected output/artifact, and relevant failure recovery. Document changed
   defaults, CLI/environment/config precedence, and interactive/non-interactive
   behavior. Mark hardware or destructive actions explicitly; never include secrets.
4. Write the shortest useful procedure: purpose, conditions, steps, expected
   result, then meaningful exceptions. Keep advanced contracts separate. Provide
   one runnable happy-path example and an edge case only when it clarifies a real
   decision or failure; use explicit placeholders for user-supplied values.
5. Maintain one parameter table or reusable example per topic and link to it.
   Extend the existing owner before creating a page. Use targeted `rg` for old
   and new setting names, UI labels, commands, or flags across the affected sources
   and guides to find stale references. For removals, delete obsolete instructions
   and explain replacements or migration when users need them. Preserve inbound
   anchors when moving content, or update their repository links.

For example, changing a cup threshold requires its owning settings table and any
affected numerical example, not a new README section. Adding a build-script flag
requires its script reference entry; update the build walkthrough only if users
need the flag there.

### Completion check

Review the final diff for contradictory values, broken reading order, and
unnecessary duplication. Verify example names, units, syntax, and calculations
against the final implementation. Run relevant safe example/contract checks when
available; never install dependencies, contact hardware, flash, or run OTA implicitly.

Classify and validate **all changed files**, including code and docs, using
[VALIDATION.md](../VALIDATION.md). Markdown does not automatically mean R0.
The offline link check does not certify external URLs or example behavior.
In the completion report, name the updated guides (or give a specific reason no
documentation was needed), checks performed, and any unverified claims or manual
evidence still required. Do not present assumed behavior as tested behavior.
