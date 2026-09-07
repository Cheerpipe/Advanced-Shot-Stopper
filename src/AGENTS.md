# Firmware instructions

- Editable sources are `src/*.{h,cpp,ino}` and `src/ble/`; generated
  `ShotStopperVersion.h` and `ShotStopperWebAssetsGzip.h` must be regenerated,
  not hand-edited. `ShotStopperWebAssets.h` is a stable source-side contract.
- Read `docs/ARCHITECTURE.md`, `docs/STATE_MACHINES.md`, and
  `docs/RESOURCE_OWNERSHIP.md` only when the affected boundary requires them.
- Control, safety, machine, relay, ISR, watchdog, boot, GPIO, and remote-control
  changes are R3. Preserve the root safety invariants and external HTTP, BLE,
  OTA, serial, persistence, and Web UI contracts.
- Keep target-specific code behind existing boundaries. Run the gate selected by
  `VALIDATION.md`; record HIL/manual work as pending rather than simulating it.
