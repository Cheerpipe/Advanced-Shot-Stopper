# Tooling instructions

- `scripts/dev` is the canonical interface; existing scripts remain compatible.
- Tooling must be non-interactive in CI, avoid implicit network/bootstrap work,
  normalize exit codes, redact secrets, and preserve complete run logs.
- Never weaken image verification. Non-interactive flash and OTA require an
  explicit confirmation, and no command may contact hardware unexpectedly.
- Generated reports belong under ignored `artifacts/` or `reports/`. Shell
  compatibility scripts must remain valid under the project-supported Bash.
