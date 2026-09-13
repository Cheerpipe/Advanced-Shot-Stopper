# Build scripts

`./scripts/dev` is the canonical developer entry point. It classifies risk,
runs incremental CMake/CTest profiles, wraps the supported commands below, and
writes full logs plus a redacted JSON summary under `artifacts/runs/`. Use
`./scripts/dev --help`.

Developer scripts under `scripts/`. Walkthrough from clone to flash:
[Build environment](BUILD.md).

**Supported: the `*-idf` scripts.** Arduino-era aliases without `-idf` have
been removed; `./scripts/dev` remains the preferred public interface.

These are **not** the USB firmware commands (`HELP`, `FACTORY_RESET`, …).
Those live in [USB serial CLI](SERIAL_CLI.md).

## Common developer tasks

| Task | Command |
| --- | --- |
| Find the relevant area | `./scripts/dev context control` |
| Classify current changes | `./scripts/dev classify` |
| Validate current changes | `./scripts/dev validate` |
| Focused host / Web / BLE / OTA / tooling tests | `./scripts/dev test normal`, `web`, `ble`, `ota`, or `tooling` |
| Sanitizers | `./scripts/dev test asan` / `./scripts/dev test tsan` |
| List build profiles | `./scripts/dev profiles` |
| Compile without contacting hardware | `./scripts/dev build --hardware esp32-s3-relay-x1-speaker --machine rancilio-silvia-pro-x` |
| Analyze an existing firmware build | `./scripts/dev analyze --arch n16r8` |
| USB install / Wi-Fi update | `./scripts/dev flash --confirm …` / `./scripts/dev ota --confirm …` |

The facade rejects passwords in argv and requires explicit confirmation for
flash/OTA. Enter passwords at the prompt or supply
`SHOTSTOPPER_DEVICE_PASSWORD` through your environment's secret mechanism.
`dev build` supplies empty extra flags unless explicitly provided; the direct
scripts below can reuse saved flags. Complete validation follows
[VALIDATION.md](../VALIDATION.md), not just a convenient focused test.

The documentation scan checks canonical guides and excludes local working files
in `temp/`, `docs/plans/`, and `docs/audits/`. These directories remain inside
the local project, Git-ignored and accessible from the IDE; see
[local storage and Git](AI_WORKFLOW.md#local-project-files-and-git).

For memory evidence, `scripts/p2_soak.py` uses byte-valued stack thresholds
(`--min-stack-bytes`; deprecated `--min-stack-words` alias also takes bytes),
rejects missing required memory/stack samples, and supports repeated
`--require-task NAME` checks during bounded profiler captures. See
[resource budgets](P2_RESOURCE_BUDGETS.md#combined-heaptiming-soak) for limits,
profiler duration constraints, offline self-tests and release evidence.

## How parameters are resolved

For the direct scripts, parameters come in this order:

1. A named flag
2. Its environment variable
3. The `.shotstopper` file at the repository root
4. An interactive prompt (Enter accepts the value in brackets)

Web UI language is deliberately transient: it uses
`--webui-language`, then `SHOTSTOPPER_WEBUI_LANGUAGE`, then `en`. It is never
read from or written to `.shotstopper`, and it never prompts.

If `--port` is missing, or the saved/CLI path is not a present device node,
flash and monitor scripts prompt like OTA does for the device password: they
list detected USB-CDC ports (`/dev/cu.usbmodem*` on macOS, `/dev/ttyACM*` on
Linux), suggest the first match, and accept Enter or a typed path. The chosen
port is saved to `.shotstopper`.

App CDC enumerates only when **GPIO 4 is jumpered to GND at reset**
([Hardware](HARDWARE.md)), unless you compiled with
`-DSHOT_STOPPER_ENABLE_JTAG=1`. Without the jumper, `monitor-idf` has no port
while the app is running. `flash-idf` still works via **BOOT + RST** (ROM
USB download) or use **OTA**. ROM download does not need the jumper.

After a successful run, ordinary non-secret values such as port and speed are
saved. Hardware and machine selections are deliberately transient, so every
build must pass both again (or provide their environment variables). The
**device password is never stored or suggested** — enter it at the hidden
prompt or provide `SHOTSTOPPER_DEVICE_PASSWORD` every time. Avoid password
flags in shell history or process arguments.

`.shotstopper` is created mode `600` and is gitignored.

For CI or a non-TTY terminal, pass flags or environment variables. If
anything required is missing, the script exits with an error instead of
prompting. The same applies with `SHOTSTOPPER_NONINTERACTIVE=1`.

## Flags

| Flag | Environment variable | Meaning |
| --- | --- | --- |
| `-p`, `--port` | `SHOTSTOPPER_PORT` | Serial port, e.g. `/dev/cu.usbmodem2101` (macOS) or `/dev/ttyACM0` (Linux). |
| `-a`, `--arch` | `SHOTSTOPPER_ARCH` | Architecture for analysis or a legacy existing image. A firmware build derives it from hardware. |
| `-s`, `--speed` | `SHOTSTOPPER_SPEED` | Serial monitor baud, e.g. `115200`. |
| `-H`, `--host` | `SHOTSTOPPER_HOST` | Controller IP or hostname for OTA. |
| `-t`, `--password` | `SHOTSTOPPER_DEVICE_PASSWORD` | Direct-script compatibility only; prefer hidden prompt/environment. The dev facade rejects secret argv. Never persisted. |
| `-f`, `--flags` | `SHOTSTOPPER_FLAGS` | Extra compile flags, as a single string. |
| `-i`, `--image` | `SHOTSTOPPER_IMAGE` | Firmware `.bin` to use for flash or OTA instead of the normal build output. Checked locally and never persisted. |
| `-b`, `--build-dir` | `SHOTSTOPPER_BUILD_DIR_OVERRIDE` | Build directory (`static`/`static-idf` only). |
| `-o`, `--output-dir` | `SHOTSTOPPER_OUTPUT_DIR` | Reports directory (`static` / `static-idf` only). |
| `--webui-language` | `SHOTSTOPPER_WEBUI_LANGUAGE` | Compile-time Web UI language (default `en`; never persisted). |
| `--hardware` | `SHOTSTOPPER_HARDWARE` | Exact built-in hardware ID or explicit JSON path. Must be paired with `--machine`; never persisted. |
| `--machine` | `SHOTSTOPPER_MACHINE` | Exact built-in machine ID or explicit JSON path. Must be paired with `--hardware`; never persisted. |
| `--development` | — | Adds development mode to this build only; never persisted. |
| `--force` | — | OTA scripts only: commit without an interactive prompt and wait for the rebooted firmware to confirm itself through the HTTP API. No Web UI reload is required. |
| `--no-check` | — | USB flash only: skip the local image identity check and transfer existing build outputs as-is. Resumable OTA rejects this flag because it requires the image identity and SHA-256. |
| `--discard-ota-session` | — | OTA only: explicitly discard a different partial or staged image. A matching image resumes automatically without this flag. |
| `-h`, `--help` | — | Show the script help. |

`--hardware-config`, `--machine-config`, `SHOTSTOPPER_HARDWARE_CONFIG`, and
`SHOTSTOPPER_MACHINE_CONFIG` are temporary deprecated aliases. New commands
should use the shorter names above.

### Web UI language

`--webui-language EN` and `--webui-language=EN` both select the English
catalog. Codes are trimmed, lower-cased, and normalized from `_` to `-`.
Regional codes try an exact catalog and then their base, so `En-en` currently
resolves to `en`. A different base such as `es-CL` fails until `es-cl.json` or
`es.json` exists; it never falls back to English. The only shipped catalog is
`src/web/locales/en.json`.

Selection is compile-time. Build owners (`build-idf`, build/flash/OTA wrappers,
warnings builds, GCC analyzer builds, and `dev validate`) forward the option to
the asset generator. Flash, OTA, and monitor commands consume existing images
and do not accept or reinterpret it. Examples:

```sh
./scripts/build-idf --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x --webui-language EN
SHOTSTOPPER_WEBUI_LANGUAGE=en ./scripts/dev build \
  --hardware esp32-s3-relay-x1-speaker --machine la-marzocco-linea-micra
./scripts/dev validate --risk R1 --webui-language en src/web
node scripts/localize_web_ui.js --check --webui-language EN
```

The selected strings are rendered before minification and gzip into
`src/ShotStopperWebAssetsGzip.h`; catalogs and resource keys are not embedded.
Adding a complete locale catalog does not change any firmware until selected.

Suggested `--flags` at the prompt (Enter accepts them):
`-Werror=deprecated-copy -DSHOT_STOPPER_ENABLE_BUZZER=1`.
Remote machine control stays off unless you add
`-DSHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=1`.

For local development only, add `--development` to bypass WebUI admin unlock
(Admin / Diagnostic / Home Actions without the device password). It affects
only that invocation and is never persisted. Do not ship development builds to
production devices.

Named profiles are selected with both profile options:

```sh
./scripts/dev build \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --flags "-DSHOT_STOPPER_ENABLE_BUZZER=1"
```

See [Hardware and machine build profiles](BUILD_PROFILES.md) for the complete
JSON contracts, capability matching, overrides, and generated artifacts.

Profile builds use `build-idf/<hardware>--<machine>/`. JSON supplies the base,
then explicit `--flags` override supported values or add unrelated definitions,
and the complete resolved configuration is validated before compilation.
Hardware and machine selections plus Web UI language are transient;
`.shotstopper` never turns them into a later build's surprise input.
Development mode can only be supplied through the CLI, never through either
JSON profile.

USB Serial/JTAG stays **off** unless you add `-DSHOT_STOPPER_ENABLE_JTAG=1`.
That build turns the IDF USB Serial/JTAG console on at boot (OpenOCD + CDC,
no GPIO4 jumper). Default firmware and any flash without that flag keep JTAG
off.

`idf.py` does not reconfigure when only the environment changes. Changing
`--flags` (for example `-DSHOT_STOPPER_MACHINE_TYPE=1`) drops the IDF CMake
cache so the new `-D` flags actually reach the compiler. Diagnostic **Type**
is that compile-time machine type (`paddle`, `momentary`, or `momentary_reed`),
not a runtime setting. Diagnostic **Compile flags** also shows the resolved
hardware profile, machine profile ID, machine brand, and machine model.
Architecture-only firmware builds are rejected instead of inventing an
installation identity.

## ESP-IDF (supported)

Writes to `build-idf/<hardware-id>--<machine-id>/` (`shotstopper.bin`).

From a fresh shell, prefer activating the exact 6.1.x installation created by
EIM before running these scripts. They reuse that environment only when its
`IDF_PATH`, pinned Python environment, `idf.py`, and version are valid. A stale
or mismatched active environment is discarded; the fallback sources the
inactive SDK selected by `IDF_PATH`, or `$HOME/esp/esp-idf-v6.1`. See
[Build environment](BUILD.md#3-install-esp-idf-required).

| Script | Alias | Required | Description |
| --- | --- | --- | --- |
| `./scripts/build-idf` | `b-idf` | `--hardware`, `--machine` (`--flags`, `--development`, `--webui-language` optional) | Resolve and validate the pair, generate version and the Web UI, then build with ESP-IDF. |
| `./scripts/flash-idf` | `f-idf` | `--port` and both profiles; `--arch` remains for a legacy existing image | Flash existing build outputs (or `--image <path>`); never rebuilds. Does not open the monitor. |
| `./scripts/monitor-idf` | `m-idf` | `--port`, `--speed` | IDF serial monitor (Ctrl+] to exit). |
| `./scripts/ota-idf` | `o-idf` | both profiles, `--host`, `--password`; `--arch` remains for a legacy existing image | Wi-Fi update with the already-built IDF binary, or `--image <path>`. |
| `./scripts/static-idf` | `s-idf` | `--arch` | Cppcheck against the IDF compilation database. Does not build. |
| `./scripts/static-tidy-idf` | | `--arch` | clang-tidy (Espressif esp-clang) against the IDF compilation database. Does not build. [Static analysis](STATIC_ANALYSIS.md). |
| `./scripts/iwyu-idf` | | `--arch` | Include-What-You-Use against the IDF compilation database. Advisory report, does not build. [Static analysis](STATIC_ANALYSIS.md). |
| `./scripts/bf-idf` | | `--port` and both profiles | build-idf then flash-idf (no rebuild or image re-check at flash time). |
| `./scripts/bfm-idf` | | `--port`, `--speed`, and both profiles | build-idf, flash-idf (no rebuild or image re-check), monitor-idf. |
| `./scripts/bo-idf` | | both profiles, `--host`, `--password` | build-idf then ota-idf; the OTA step reads the built identity for safe resume. |
| `./scripts/bsfm-idf` | | `--port`, `--speed`, and both profiles | build-idf, static-idf, flash-idf (no rebuild or image re-check), monitor-idf. Does not flash if analysis reports diagnostics. |
| `./scripts/gcc_analyzer` | | both profiles (`--flags` optional) | Build with GCC `-fanalyzer` into `reports/gcc-analyzer/`. |

Examples (flash/OTA commands affect hardware; complete bench checks first):

```sh
# Build, flash, and monitor; prompts for missing parameters and remembers them
./scripts/bfm-idf

# Explicit (macOS CDC port)
./scripts/build-idf --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x
./scripts/flash-idf --port /dev/cu.usbmodem2101 \
  --hardware esp32-s3-relay-x1-speaker --machine rancilio-silvia-pro-x
./scripts/monitor-idf -p /dev/cu.usbmodem2101 -s 115200

# Linux
./scripts/bfm-idf -p /dev/ttyACM0 -s 115200 \
  --hardware esp32-s3-relay-x1-speaker --machine rancilio-silvia-pro-x

# Wi-Fi update
./scripts/bo-idf --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x --host 192.168.1.50
```

`--force` is accepted by `ota-idf` and `bo-idf` (including `o-idf`). It bypasses
the final commit prompt, then polls the controller until
the boot ID changes, the running image digest matches the local file and
`confirmed: true`, or four minutes pass. Missing evidence on older firmware is
reported as unverified. Without `--force`, completion of commit does not claim
that the rebooted image has been confirmed.

`--no-check` is accepted by `flash-idf` and its USB flashing wrappers
(`bf-idf`, `bfm-idf`, and `bsfm-idf`, which forward it to the install step).
For USB flash it skips the local identity check; both checked and unchecked
project transfers use the existing `flash_args` with esptool (bootloader,
partition table, otadata, and app) and never rebuild. OTA rejects it because
resumable sessions cannot be matched or committed safely without the local
SHA-256, architecture, hardware profile, machine profile, and version.

`--erase-all` is accepted by `flash-idf` and its USB flashing wrappers. It
runs a full `erase_flash` before writing the project bootloader, partition
table, initial OTA metadata, and application. This is required when moving a
controller from a legacy layout or one without the architecture-specific 40 KiB
`shotcurve` partition:

```sh
./scripts/flash-idf --port /dev/cu.usbmodem2101 \
  --hardware esp32-s3-relay-x1-speaker --machine rancilio-silvia-pro-x \
  --erase-all
```

This permanently removes both firmware slots, Wi-Fi credentials, all settings
and presets, calibration, BLE preferences, shot history, and last-shot data.
There is no data migration. `--erase-all` is rejected with `--image`, because
an external application image cannot reconstruct the bootloader and partition
table. A normal USB flash reads the installed partition table first and refuses
the legacy NVS size or a missing/mismatched `shotcurve` label, data type, custom
subtype, offset, or size. This preflight also applies to external images because
they leave the installed partition table untouched. Blank, explicitly erased,
and compatible devices write every entry from the existing `flash_args`
directly. External images use the installed `app0` offset instead of assuming a
fixed address.

When the controller already owns a partial or staged different image, OTA
stops without modifying it and prints both identities. Re-run with
`--discard-ota-session` only when discarding that remote image is intentional.
A matching image automatically adopts the existing `transferId` and resumes
from the controller's validated `nextOffset`. This offset can retreat after
reboot to the last 512 KiB journal checkpoint; clients reconstruct the range
from that offset, with 4 KiB alignment except at the image end.

OTA clients use a 10-second connection timeout and retain the existing long
transfer window. A transient transport failure is reconciled with the OTA
status endpoint before retrying, then retried at most twice when the controller
is idle. Safety and validation failures are not retried.

`--image` can also be passed to the flash/OTA wrappers (`bf-idf`, `bfm-idf`,
`bo-idf`, and `bsfm-idf`); they still execute their named build or
analysis steps, then use the selected image for the transfer. The image is
checked against the selected profile identities before transfer. For an IDF USB flash,
an external image is written to the `app0` offset read from the installed
partition table; it does not replace the bootloader or partition table.
