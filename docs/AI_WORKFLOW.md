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
