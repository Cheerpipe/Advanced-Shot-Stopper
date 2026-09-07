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

## Interruption and resume

For work likely to span sessions, maintain the ignored
`docs/handoff/SESSION_HANDOFF.md`. Keep it below 8 KiB and 120 lines with:
objective, risk, base SHA, worktree state, decisions, invariants, tasks, exact
next action, files, commands/results, artifacts, and blockers. On resume, compare
the recorded SHA and current diff before trusting prior results. Delete the file
when complete; move durable decisions into canonical docs or a short ADR.
