# Repository agent rules

## Start here

Read only this file, `docs/README.md`, and the nearest scoped `AGENTS.md` for
the area being changed. Expand context only when those files point to it.
Never read `docs/plans/` or `docs/audits/` content proactively for context.
Read a plan or audit only when the user explicitly asks to read or work with it;
documentation links, task similarity, and session recovery do not grant access.

## Priorities and scope

1. Safety and correctness.
2. Root-cause understanding and preservation of existing behavior.
3. Proportionate validation.
4. Small, maintainable changes and efficient context use.

Prefer targeted `rg` searches and focused reads. Do not load generated assets,
build trees, managed components, dependencies, or reports wholesale. Edit
existing logic instead of adding wrappers or parallel implementations. Explain
before adding more than 15 lines for a fix. Never leave placeholder or commented
out code.

## Safety and permissions

Treat changes to relay, machine control, ISR, watchdog, boot, GPIO, partitions,
OTA safety, or remote activation as R3. Preserve these invariants:

- the relay is open on startup, reset, power loss, and safety failure;
- boot readiness includes every dependency required for safe actuation;
- timing limits and watchdog coverage remain enforced;
- remote machine activation is disabled by default;
- shared resources retain one explicit owner.

Never flash hardware, run OTA, control the relay, or execute HIL unless the user
explicitly requests it. Never expose credentials in argv, logs, or artifacts.
Unknown paths are R3; risk overrides may only increase risk.

## Workflow and validation

Use `./scripts/dev context <area>` for orientation and `./scripts/dev classify`
before changing code. Follow `VALIDATION.md`; missing required dependencies are
failures, not silent skips. Tests must not install packages, use the network, or
touch hardware implicitly. Static analysis is run only when explicitly requested
or required by the requested validation gate.

Keep unrelated user changes. Use `apply_patch` for edits. Store temporary working
files in task-specific `temp/ai_temp_<task>/` directories and clean up only files
no longer needed for evidence or recovery. Never
commit unless explicitly asked. A completed change report includes an English
commit title, a list of changes made, and a brief but complete technical
description, all in English.

## Local project storage

Git-ignored files are part of the local project and must stay accessible from
the IDE. "Outside Git" never means outside the project directory. Keep temporary
working files in `temp/`, plans in `docs/plans/`, and audits in `docs/audits/`;
all three directories must remain Git-ignored. Bring existing external
task working files into the appropriate local directory and update their
references. Follow [local storage, migration, and cleanup rules](docs/AI_WORKFLOW.md#local-project-files-and-git).

## Plans, audits, and session handoff

Store every development plan in `docs/plans/` and every audit in `docs/audits/`;
both directories must remain Git-ignored. Before multi-step implementation,
create the task's plan; before an audit, create its audit file. Resume an existing
record only when the user explicitly requests reading or working with it.
Each file is its own authoritative progress record and session handoff;
do not create separate handoffs or duplicate an audit checklist in a plan.
Follow the shared format and recovery procedure in
[AI workflow](docs/AI_WORKFLOW.md#plans-audits-and-session-handoff).

Give every step and substep a stable ID, an explicit completion status, and a
Markdown checkbox. Update the file immediately when any part is completed,
including its evidence and the next action. These rules apply equally to plans
and audits. Keep completed files locally for traceability; do not automatically
delete them or add/force-add them to Git.

## Documentation

Canonical documentation is concise technical English and belongs in `docs/`.
For changes to settings, scripts, interfaces, workflows, or observable behavior,
read and follow [Documentation maintenance](docs/AI_WORKFLOW.md#documentation-maintenance).
Update the affected canonical guides in the same change, including renames and
removals. Before finishing, report which docs changed or why none were needed;
do not treat a passing link check as verification of documented behavior.

Keep task-specific progress, decisions, and recovery notes in the corresponding
plan or audit file; audit findings belong in `docs/audits/`. Durable behavior
belongs in the canonical guides, which must not depend on ignored working files.
