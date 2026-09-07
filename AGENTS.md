# Repository agent rules

## Start here

Read only this file, `docs/README.md`, and the nearest scoped `AGENTS.md` for
the area being changed. Expand context only when those files point to it. On
resume, read `docs/handoff/SESSION_HANDOFF.md` first when it exists.

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
explicitly requests it. Never expose credentials in argv, logs, artifacts, or a
handoff. Unknown paths are R3; risk overrides may only increase risk.

## Workflow and validation

Use `./scripts/dev context <area>` for orientation and `./scripts/dev classify`
before changing code. Follow `VALIDATION.md`; missing required dependencies are
failures, not silent skips. Tests must not install packages, use the network, or
touch hardware implicitly. Static analysis is run only when explicitly requested
or required by the requested validation gate.

Keep unrelated user changes. Use `apply_patch` for edits, prefix temporary files
inside the repository with `ai_temp_`, and remove them when finished. Never
commit unless explicitly asked. A completed change report includes an English
commit title and English change summary.

## Plans and interrupted sessions

Explicit plans live in `docs/plans/` as numbered, resumable checklists. Flip each
checkbox immediately after completing it; never batch progress at the end.
Long work uses the ignored `docs/handoff/SESSION_HANDOFF.md`, capped at 8 KiB or
120 lines. Record objective, risk, base SHA, worktree, decisions, invariants,
tasks, next action, files, commands/results, artifacts, and blockers. On resume,
verify SHA/diff and reuse valid results whose inputs did not change. Delete the
handoff at completion and promote durable decisions to docs or an ADR.

Canonical new documentation is concise technical English. Official docs belong
in `docs/`, investigations in `docs/audits/`, plans in `docs/plans/`, and only
temporary session state in `docs/handoff/`.
