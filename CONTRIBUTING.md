# Contributing

1. Discover: read `AGENTS.md`, `docs/README.md`, and the nearest scoped
   `AGENTS.md`; use `./scripts/dev context <area>` before opening broad files.
2. Classify risk: run `./scripts/dev classify <changed paths>`. Unknown paths
   are R3. State affected safety invariants before editing.
3. Change: make the smallest coherent edit. Do not combine source moves with
   features, GPIO changes, NVS migrations, or relay behavior changes.
4. Validate: run the gate in `VALIDATION.md`. Missing required dependencies are
   failures. Never flash, OTA, operate the relay, or run HIL implicitly.
5. Document: update the canonical document that owns any changed public
   architecture, safety, or interface contract.
6. Deliver: report risk, tests, full-log artifact, manual/HIL work still due,
   and an English proposed commit title. Do not commit unless requested.

Use concise technical English for canonical documentation. Do not add plans,
audits, decision logs, handoffs, or other development-session records to
`docs/`.
