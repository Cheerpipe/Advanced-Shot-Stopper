# Safety service boundary

Safety remains behind the stable root headers while R3 extractions require HIL.
Do not move relay, timer, watchdog, boot, or GPIO behavior together with a
functional change. The root `AGENTS.md` invariants and `VALIDATION.md` R3 gate
apply to every file in this directory.
