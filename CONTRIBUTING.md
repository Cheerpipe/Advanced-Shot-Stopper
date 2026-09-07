# Contributing

1. Discover: read `AGENTS.md`, `docs/README.md`, and the nearest scoped
   `AGENTS.md`; use `./scripts/dev context <area>` before opening broad files.
2. Classify risk: run `./scripts/dev classify <changed paths>`. Unknown paths
   are R3. State affected safety invariants before editing.
3. Change: make the smallest coherent edit. Do not combine source moves with
   features, GPIO changes, NVS migrations, or relay behavior changes.
4. Validate: run the gate in `VALIDATION.md`. Missing required dependencies are
   failures. Never flash, OTA, operate the relay, or run HIL implicitly.
5. Document: update public contracts and add a short ADR under
   `docs/decisions/` for durable architecture, safety, or interface decisions.
6. Deliver: report risk, tests, full-log artifact, manual/HIL work still due,
   and an English proposed commit title. Do not commit unless requested.

Use concise technical English for new canonical documentation. Historical
investigations belong in `docs/audits/`; resumable plans in `docs/plans/`; the
ignored `docs/handoff/SESSION_HANDOFF.md` is temporary and deleted on completion.
