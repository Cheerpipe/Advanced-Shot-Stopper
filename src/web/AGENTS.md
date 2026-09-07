# Web UI instructions

- Edit `src/web/app.css`, `src/web/app.js`, `src/web/html/`, and `src/web/js/`.
  Never edit generated `src/ShotStopperWebAssetsGzip.h` by hand.
- Preserve progressive enhancement, admin-lock behavior, remote-actuation
  lockdown, endpoint schemas, and the embedded asset budget.
- `node_modules/` is dependency output and must not be loaded wholesale.
  Bootstrap explicitly with `npm ci`; tests never install packages.
- Web-only changes are R1 unless they touch OTA or remote control, which are at
  least R2 and R3 respectively.
