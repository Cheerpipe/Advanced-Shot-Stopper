# Tooling instructions

- `scripts/dev` is the only supported public firmware interface. Focused stage
  implementations under `scripts/internal/` are private and may change.
- Tooling must be non-interactive in CI, avoid implicit network/bootstrap work,
  normalize exit codes, redact secrets, and preserve complete run logs.
- Never weaken image verification. Non-interactive flash and OTA require an
  explicit confirmation, and no command may contact hardware unexpectedly.
- Generated reports belong under ignored `artifacts/` or `reports/`. Shell
  compatibility scripts must remain valid under the project-supported Bash.
