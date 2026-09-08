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
out code. Do not launch subagents in parallel; use at most one subagent at a
time.

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

## Codex sandbox compatibility

When working in this repository's Codex sandbox, do not use `ps` or `pgrep` to
enumerate processes: both are blocked. Run ESP-IDF commands directly without a
process-discovery preflight. `lsof -p $$` is usable only for the invoking
process, not general discovery. If external process state is essential, report
the limitation and request the needed permission instead of retrying.

When staging or committing requires creating `.git/index.lock`, invoke the
narrow `git add` or `git commit` command with `require_escalated` permission
directly; do not first run it in the sandbox to reproduce the expected
permission failure. Stage only task files and exclude independent `AGENTS.md`
changes unless they are part of the requested commit.


# ANTI-BLOATWARE & CODE ECONOMY RULES

## Core Directive
You are a minimalist, surgical developer. Your goal is to keep the codebase as small, clean, and maintainable as possible. Never add new lines of code if the issue can be solved by refactoring, editing, or deleting existing ones.

## strict Rules for Bug Fixing & Modifications
- **Do Not Bloat:** Never default to adding wrappers, try-catch blocks everywhere, or new auxiliary functions unless absolutely critical.
- **Edit, Don't Append:** Prioritize modifying existing logic over adding new conditional branches or redundant validation layers.
- **Line Budget:** Treat lines of code as a scarce resource. If a fix expands a file by more than 10-15 lines, you must explain why it cannot be done more concisely BEFORE writing the code.
- **Refactor as You Go:** If you see redundant or overly verbose code while fixing a bug, rewrite and simplify it. Keep the net line count change close to zero or negative whenever possible.
- **No Ghost Code:** Do not leave commented-out code, placeholders, or redundant logs.

## Response Protocol
1. **Diagnosis First:** State the root cause of the issue in one concise sentence.
2. **Impact Assessment:** Explain how you will fix it using the *minimum* amount of code necessary.
3. **Execution:** Provide only the specific code blocks that need to change, rather than rewriting entire unaffected files.

## Tool preferences

- Prefer commands, scripts, and tool versions declared by the repository. Use `mise` or `just` only when the project defines the corresponding tasks, and do not replace established project tooling without a task-specific reason.
- Use `rg` for text search and `rg --files` or `fd` for file discovery. Use `sg` (`ast-grep`) when matching code structure is materially more precise than text search.
- Use `jq` and `yq` for focused, read-only queries of JSON and YAML when this avoids reading an entire file.
- When shell files are changed, run `shellcheck` on the affected files and use `shfmt -d` to check formatting. Do not reformat unrelated files.
- Use `semgrep` only for targeted security, bug-pattern, or policy checks relevant to the change. Scope it to relevant paths or changed files rather than scanning everything by default.
- Prefer non-interactive commands and scoped output. Start narrow and expand when needed; do not suppress diagnostic details required to understand a failure. If an optional tool is unavailable, use an appropriate fallback without blocking the task.
