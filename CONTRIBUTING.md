# Contributing

Start with `AGENTS.md`, the [documentation index](docs/README.md), and the
nearest scoped `AGENTS.md`. Set up dependencies using [Build](docs/BUILD.md);
tests never install packages or contact hardware implicitly.

## One change, from discovery to validation

1. Find the area with `./scripts/dev context <area>`. Use `build` for toolchain
   work, `web` for UI, and `control` for brewing; all areas are listed in
   [the developer map](docs/README.md#developer-map).
2. Run `./scripts/dev classify <paths…>` before editing. Unknown paths are R3;
   safety-related documentation can also be R3. State affected invariants.
3. Make a coherent change, preserving unrelated work. Do not combine source
   moves with GPIO, relay behavior, NVS migrations, or unrelated features.
4. Update the canonical guide when public behavior, settings, or interfaces
   change. Link to its parameter table rather than copying it into several pages.
5. Run `./scripts/dev validate` on the final changed paths. Consult
   [VALIDATION.md](VALIDATION.md) for required gates and evidence.
6. Report risk, checks, failures, full-log location and any manual evidence
   still needed, plus an English proposed commit title. Commit only if requested.

## Examples

| Change | Discover / classify | Validate |
| --- | --- | --- |
| Non-critical README wording | `./scripts/dev classify README.md` | `./scripts/dev validate README.md` |
| Web styling | `./scripts/dev context web`; classify exact changed files | Focused `dev test web`, then the classified gate |
| Brew or switch behavior | `./scripts/dev context control` or `machine`; classify paths and state invariants | Complete R3 gate and applicable bench/HIL evidence |

Focused tests help during editing but do not replace a higher-risk gate.
Flash, OTA, relay operation, and HIL require explicit authorization.

## Documentation style

Write concise English for the reader's task: purpose, prerequisites, action,
expected result, then exceptions. Keep exact UI labels and units. Defaults
must identify their preset/build or migration context.

Use short guides and stable headings. Keep advanced contracts separate from
daily-use steps, one maintained example per integration, and relative links to
sources or related pages. Do not duplicate large payloads or configuration
blocks to support another reading path.

Canonical behavior belongs in `docs/`; development plans, audits, handoffs
and session notes do not.
