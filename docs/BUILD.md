# Build, test, and install

<a id="build-environment"></a>

Supported firmware uses ESP-IDF 6.1.x (pinned reference: **6.1**),
Arduino-ESP32 **3.3.11** as an IDF component, and native NimBLE.
The bundled EspressoScaleBLE library is not installed through Library Manager.

Production defaults compile DFS (`CONFIG_PM_ENABLE`) and S3 controller modem
sleep with the main crystal. The runtime [Power management](settings/power-management.md)
setting defaults off. The build wrapper recreates cached configurations missing
this support; automatic light sleep remains disabled.

This walkthrough covers macOS and Debian/Ubuntu Linux. Native Windows build
and hardware-installation steps are not qualified here; the Windows notes in
[Static analysis](STATIC_ANALYSIS.md#4-windows-prerequisites-native-no-wsl)
cover tool preparation only.

Read [Hardware](HARDWARE.md) before connecting equipment. Building and host tests
do not need a connected controller. USB flash sections explicitly affect it.

## 1. Clone the repository

```sh
git clone https://github.com/Cheerpipe/AcaiaArduinoBLE.git
cd AcaiaArduinoBLE
```

Use your fork's URL when contributing. Commands below run from this repository
unless a block explicitly changes directories.

## 2. Install host prerequisites

<a id="macos-maintainer"></a>

macOS, with Homebrew installed:

```sh
xcode-select --install   # only if Command Line Tools are missing
brew install git node python cmake ninja cjson
```

<a id="linux"></a>

Debian/Ubuntu:

```sh
sudo apt-get update
sudo apt-get install git python3 python3-pip python3-venv cmake ninja-build \
  wget flex bison gperf ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 \
  nodejs npm g++ libcjson-dev
```

Check `node --version`, `python3 --version`, `cmake --version`,
`ninja --version`, and `c++ --version`. CMake must understand the repository's
version-6 presets. If your distribution's packages are too old, update the
toolchain before continuing.

<a id="2-nodejs-dependencies"></a>

Install the pinned Web UI dependencies from the repository root:

```sh
npm ci
```

This is an explicit setup step. Test commands never install dependencies.

## 3. Install ESP-IDF

<a id="3-install-esp-idf-required"></a>

Prefer the [ESP-IDF Installation Manager (EIM)](https://docs.espressif.com/projects/idf-im-ui/en/latest/):
install an exact ESP-IDF **6.1.x** environment, open a fresh shell, and source
the activation script printed by EIM (or use **Open IDF Terminal** in its GUI).
For example, use the actual filename EIM created:

```sh
source "$HOME/.espressif/tools/activate_idf_v6.1.sh"
idf.py --version
```

The project scripts reuse an active environment only when `IDF_PATH`, its
`IDF_PYTHON_ENV_PATH/bin/python`, `idf.py`, and the reported 6.1.x version all
agree. A stale or mismatched active environment is discarded before fallback.

When the legacy SDK's own activation script points at a Python environment
that no longer exists on the machine (for example after a macOS or Python
upgrade), the scripts pick up an installed environment for the same 6.1
release from `~/.espressif/python_env` automatically, preferring the newest
one. If none matches, the activation error names the missing directory so you
can run the install script for it.

The supported legacy fallback is a separate SDK clone. This subshell returns
you to the repository when installation finishes:

```sh
(
  mkdir -p "$HOME/esp"
  cd "$HOME/esp"
  git clone -b v6.1 --recursive https://github.com/espressif/esp-idf.git esp-idf-v6.1
  cd esp-idf-v6.1
  ./install.sh esp32s3
)
. "$HOME/esp/esp-idf-v6.1/export.sh"
idf.py --version
```

If that SDK directory already exists, verify its version instead of cloning
over it. For an inactive legacy SDK elsewhere, export `IDF_PATH`; the scripts
source its `export.sh`. Otherwise they discover `$HOME/esp/esp-idf-v6.1`.
Every path rejects versions outside 6.1.x.

`idf/main/idf_component.yml` and `idf/dependencies.lock` pin the component
graph, including mDNS 1.13.1. First firmware builds may need network access to
resolve SDK components; prepare these dependencies before attempting an offline
validation run.

## 4. Validate before installation

`./scripts/dev` is the canonical entry point:

```sh
./scripts/dev test normal
./scripts/dev test web
```

A passing host run ends with CTest's success summary; the command prints the
full-log/JSON summary location in `artifacts/runs/`. Missing tools, cJSON, or
Node dependencies are failures, not skipped tests.

For changed code, run `./scripts/dev classify` and the complete gate from
[VALIDATION.md](../VALIDATION.md), including sanitizers/builds where required.
Host tests cannot verify wiring, radio timing, or physical stop behavior.
R2/R3 validation compiles every supported profile with the `--development`
profile, including the Linea Micra pair, so a passing gate leaves the
conservative local image ready in its normal `build-idf/<hardware>--<machine>/`
directory.

<a id="8-host-tests-before-you-flash"></a>
<a id="4-ble-backend"></a>

## 5. Build

Prefer named hardware and machine profiles. A hardware profile selects the
assembled board, memory layout, GPIO wiring, relay polarity, and installed
peripherals; a machine profile selects its control topology and factory
defaults. See [Hardware and machine build profiles](BUILD_PROFILES.md) for the
complete contracts and compatibility rules. One supported combination is:

```sh
./scripts/dev build \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x
```

Both options are required together. The X1 profile always resolves to
**n16r8** (16 MB flash / 8 MB OPI PSRAM). It writes to
`build-idf/esp32-s3-relay-x1-speaker--<machine>/` and also creates a `.bin`
whose filename contains that complete variant.

Every successful `build` also preserves the matching `shotstopper.elf` and
`shotstopper.bin` under
`artifacts/firmware/<hardware>--<machine>/<ELF SHA>/`, alongside
`identity.json`. The SHA is the one printed in an ESP32 panic log; its prefix
can locate the matching archive. These Git-ignored copies are created without
flashing and are retained by `./scripts/dev clean`. A later build cannot replace
an archive whose SHA already exists with different files.

Explicit `--flags` are applied after the JSON values. Supported profile values
are reflected in the resolved manifest; additive compiler flags keep working.
The resolver rejects unsafe or contradictory overrides, including GPIO
collisions, a machine that requires absent reed hardware, a normally-closed
relay profile, or a different `--arch`. Development mode remains CLI-only and
must never be added to a profile.

Machine defaults seed a new installation and factory reset. Valid persisted
settings survive ordinary boot. Every persistent store is schema 1; older
settings, logs, and curves are rejected and replaced with factory defaults.
Firmware installation is USB-only, so a clean flash is required for this
cutover and profile identity is checked before writing the device.

List available IDs and compatibility before building:

```sh
./scripts/dev profiles
```

Every firmware build requires both profiles. Each selector accepts an exact
built-in ID or an explicit JSON path. There is no implicit or remembered
profile selection, and an architecture-only build is rejected. `--arch`, when
also supplied, can only confirm the target derived from hardware.

The Web UI is compiled in English by default. To select it explicitly:

```sh
./scripts/dev build \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --webui-language EN
```

Language selection happens while the Web assets are generated; it does not add
a browser setting or embed every catalog. The flag overrides
`OPENBREWBYWEIGHT_WEBUI_LANGUAGE`, and an omitted selection always uses `en`. Codes
are case-insensitive, `_` is normalized to `-`, and a regional code tries its
exact catalog followed by its base language. English is currently the only
shipped catalog, so another base language fails instead of silently producing
English. Adding a complete locale file does not affect existing firmware until
that locale is selected.

The served Web UI is also machine-type exclusive: a build compiled for a paddle
machine ships only the paddle controls, a momentary build ships only the switch
timings and the forced-pulse action, and a reed build additionally exposes the
reed confirmation window. Controls for other machine types are not embedded at
generation time, so each firmware binary carries only the interface its machine
can use.

The wrapper preserves project diagnostics but hides ESP-IDF 6.1's known
`esp_wifi`/`wpa_supplicant` component-validation warnings. Extra-warning
reports retain their unfiltered SDK log and report project-owned warnings
separately.

The facade passes empty extra flags unless supplied; it does not reuse a saved
extra-flags preference implicitly.

For local development, use the development build profile
(`--no-auth-admin --jtag` combined):

```sh
./scripts/dev build \
  --hardware esp32-s3-relay-x1-speaker-reed \
  --machine rancilio-silvia-pro-x-reed \
  --development
```

Omitting the profile, or passing `--release` explicitly, builds without admin
unlock and without the JTAG console. `--no-auth-admin` and `--jtag` also work
individually on top of a release build.

Machine type is derived from `interface.control` plus `interface.feedback`; a conflicting
`OPEN_BREW_BY_WEIGHT_MACHINE_TYPE` override is rejected.

| Option | Meaning |
| --- | --- |
| `OPEN_BREW_BY_WEIGHT_MACHINE_TYPE=0/1/2` | Paddle / momentary / momentary+reed; see [machine types](../README.md#machine-types). |
| `OPEN_BREW_BY_WEIGHT_ENABLE_BUZZER=0/1` | Omit / include local passive buzzer. Follows the hardware profile's `speaker.present` by default; `=0` omits it even when a speaker is present. |
| `OPEN_BREW_BY_WEIGHT_ENABLE_JTAG=1` | Development USB Serial/JTAG at boot without the GPIO4 console jumper; build it with `./scripts/dev build --jtag` (or the `--development` profile). |
| `SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=0/1` | Remote start/rinse disabled / explicit opt-in. Default is disabled. Builds without it also omit the Home remote-action controls from the served interface. |
| `OPEN_BREW_BY_WEIGHT_DEVELOPMENT=1` | Compiles administration as public: Admin and Diagnostic need no device-password session and the UI shows no lock panel. The password-unlock endpoints are not compiled in this mode. Build it with `--no-auth-admin` or the `--development` profile. Local development only — never use for an installed machine. |

### Compiler optimization and existing sdkconfig

Supported ESP-IDF firmware builds default to `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`
from `idf/sdkconfig.defaults`, which selects GCC `-Os` (optimize for size) for
both supported architectures. After compiling, the build verifier prints the
selected optimization level (`sdkconfig: CONFIG_COMPILER_OPTIMIZATION_SIZE=y`
on default builds) and fails if the configuration does not hold it.
The `Debug` setting in the root `CMakePresets.json` applies only to host tests;
it does not change firmware optimization.

ESP-IDF offers exactly four optimization levels; `-O1` and `-O3` are not
selectable and the wrapper rejects them. To compile an experiment at another
level, pass exactly one of the transient build options; omitting them keeps
the default `-Os` build:

```sh
./scripts/dev build --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x --os
```

| Option | GCC level | Kconfig choice |
| --- | --- | --- |
| `--o0` | `-O0` | `CONFIG_COMPILER_OPTIMIZATION_NONE` |
| `--og` | `-Og` | `CONFIG_COMPILER_OPTIMIZATION_DEBUG` |
| `--o2` | `-O2` | `CONFIG_COMPILER_OPTIMIZATION_PERF` |
| `--os` | `-Os` | `CONFIG_COMPILER_OPTIMIZATION_SIZE` (default) |

The options are mutually exclusive, apply to that invocation only, and are
never persisted. They work through a generated defaults file appended to
`SDKCONFIG_DEFAULTS`; when an existing sdkconfig holds a different level, the
build recreates the configuration so the requested level takes effect, which
discards every other local `menuconfig` choice stored in that file. A later
build without a level option restores `-Os` the same way.
The versioned image and memory baselines were measured with `-Os` and apply
only to `-Os` builds. Other levels still check the OTA partition limit,
external BSS ceiling, and required internal-memory placement; their image
sizes are experimental and do not establish a qualified resource baseline.

The final verifier also rejects drift from the qualified production profile:
n8r4 uses 8 MB flash, `partitions-n8r4.csv`, and QUAD PSRAM; n16r8 uses 16 MB
flash, `partitions-n16r8.csv`, and OCT PSRAM. Both require DIO/80 MHz flash,
80 MHz PSRAM, a 32 KiB internal reserve, 64 KiB MMU pages, rollback support,
the mDNS task stack and dynamic responder allocations in PSRAM, the pinned
boot/task/interrupt watchdog and panic settings, and the GPTimer ISR handler in
IRAM. Other application, NimBLE/VHCI, HTTP, persistence, control, and
flash-writing stacks remain internal. These checks verify current hardware
settings; they do not retune clocks, partitions, or watchdog durations.

Defaults seed a new `build-idf/<hardware-id>--<machine-id>/sdkconfig`; they do
not overwrite an existing file. Inspect one current tree with:

```sh
grep '^CONFIG_COMPILER_OPTIMIZATION' \
  build-idf/esp32-s3-relay-x1-speaker--rancilio-silvia-pro-x/sdkconfig
```

To restore all repository defaults for that architecture, remove its generated
configuration and rebuild. This also discards every other local `menuconfig`
change stored in that file:

```sh
rm build-idf/esp32-s3-relay-x1-speaker--rancilio-silvia-pro-x/sdkconfig
./scripts/dev build --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x
```

Use the directory produced by the exact selected pair. Do not add optimization
flags through `--flags`; the supported optimization contract is the Kconfig
selection above, which the build verifier checks. The `menuconfig` route for
the optimization level is gone: the wrapper recreates the configuration
whenever the level differs from the requested one.

Other choices in an existing IDF `sdkconfig` are retained in the same way, as
long as the optimization level does not change.
Omitting a macro does not always mean its feature is off; explicit flags
override matching choices. Review Diagnostic build identity and options before
installation.

The build renders the selected catalog into
`src/OpenBrewByWeightWebAssetsGzip.h`, generates version identity and
`build-idf/<hardware-id>--<machine-id>/openbrewbyweight.bin`, then checks image and
memory budgets. Use only the image for the intended profile pair. The supported partition layouts have
two app slots; arbitrary 4 MB layouts cannot hold this firmware.

The n16r8 layout reserves 640 KiB for a temporary ESP-IDF core dump and
1,408 KiB for two saved crash records. Its unused FFAT area is 7,944 KiB
(`0x7C2000` bytes). The n8r4 layout keeps its existing 64 KiB core-dump
partition but does not enable persistent crash capture. On n16r8, the mDNS
task stack is internal so that stack remains available to a core dump.

GitHub Actions publishes the three official validation pairs listed in
[Build profiles](BUILD_PROFILES.md#capability-matching). Names follow
`shotstopper-ota-<profile>-jtag-off-remote-off.bin`; those two features are
explicitly disabled at compile time. GitHub downloads each artifact as a ZIP
container, but that container holds only the named `.bin` file.

## 6. Flash (USB)

Proceed only after reviewing the image, board, wiring and applicable
[bench checks](MANUAL_TEST_PLAN.md). Keep the machine activation circuit
disconnected for initial verification.

Connect a USB **data** cable. Use BOOT + RST for ROM download if the app exposes
no port. App serial monitoring needs the
[GPIO4 jumper or a JTAG build](HARDWARE.md#usb-console-jumper).

Replace the port below with the detected controller port:

```sh
./scripts/dev flash --confirm --port /dev/cu.usbmodem2101 \
  --hardware esp32-s3-relay-x1-speaker --machine rancilio-silvia-pro-x
```

To build and install the exact result in one command, put the stages before
their options:

```sh
./scripts/dev build flash --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --port /dev/cu.usbmodem2101
```

Add `monitor` to open the serial console only after both earlier stages pass:

```sh
./scripts/dev build flash monitor --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --port /dev/cu.usbmodem2101 --speed 115200
```

Linux commonly uses `/dev/ttyACM0` or `/dev/ttyUSB0`. Build first whenever
sources, options, or the Web UI language change: the installer never rebuilds
or relabels the selected image. Successful transfer still requires post-boot
and electrical verification.

The installer reads the partition-table sector first. Project builds always use
the existing `flash_args`, which transfers the bootloader, partition table,
initial OTA metadata and application without invoking a build. `--no-check`
only skips local identity verification. An external `--image` still replaces
app0 only on a readable installed layout.

### Clean-install cutovers

The current settings contract restarts at schema 1 and deliberately does not
migrate any earlier settings blob. Install this firmware with `--erase-all`
even when the partition table already matches. An installed 20 KiB NVS layout,
or a current layout without the dedicated
56 KiB `shotcurve` partition or the 32 KiB `shotlog` and `history` partitions,
or an n16r8 layout without the new crash-history area,
needs a one-time clean USB installation. A normal flash refuses an incompatible
layout. **The following erases both firmware
slots and all saved data:** settings, Wi-Fi credentials and password, recipes
and presets, calibration, scale preferences, shot history and last shot. There
are also no retained crash records after this installation. Settings and
curves start from factory defaults without migration.

```sh
./scripts/dev flash --confirm --port /dev/cu.usbmodem2101 \
  --hardware esp32-s3-relay-x1-speaker --machine rancilio-silvia-pro-x \
  --erase-all
```

Record the settings you need before reinstalling. Do not combine `--erase-all`
with an external `--image`; the project build outputs are required to write the
bootloader, new partition table, initial metadata and application. An app-only
image cannot activate a changed partition table.

## 7. Serial monitor and first setup

<a id="7-serial-monitor-and-cli"></a>

```sh
./scripts/dev monitor --port /dev/cu.usbmodem2101 --speed 115200
```

Exit with **Ctrl+]**. Send `HELLO` to check command access even when debug
logging is off. See [USB CLI](SERIAL_CLI.md) for troubleshooting and commands.

Verify boot identity, configured machine type and relay behavior, then continue
with [First setup and daily use](GETTING_STARTED.md).

<a id="9-update-over-wi-fi-ota"></a>

## 8. Firmware updates

Firmware installation is USB-only. Use `./scripts/dev flash --confirm` with
the exact hardware and machine profiles; Wi-Fi OTA commands are intentionally
rejected.

## One supported command interface

`./scripts/dev` is the only supported public firmware command. The old direct
IDF stage aliases and combination wrappers were removed. Focused shell files
under `scripts/internal/` are private implementation details.

## Optional static analysis

Use [Static analysis](STATIC_ANALYSIS.md) when requested or required by the
validation gate. It lists prerequisites, tool behavior and failure meanings.

## Firmware version

`VERSION` supplies the release number; the build adds the git revision and a
dirty marker when applicable. The Web UI footer and boot output identify it.
