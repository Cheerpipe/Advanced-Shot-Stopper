# Build, test, and install

<a id="build-environment"></a>

Supported firmware uses ESP-IDF **6.1.0** (release tag **v6.1**),
Arduino-ESP32 **3.3.12** as an IDF component, and native NimBLE.
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
do not need a connected controller. Flash/OTA sections explicitly affect it.

## 1. Clone the repository

```sh
git clone https://github.com/Cheerpipe/Advanced-Shot-Stopper.git AcaiaArduinoBLE
cd AcaiaArduinoBLE
```

Use your fork's URL when contributing. Commands below run from this repository
unless a block explicitly changes directories.

## 2. Install host prerequisites

<a id="macos-maintainer"></a>

macOS, with Homebrew installed:

```sh
xcode-select --install   # only if Command Line Tools are missing
brew install git python cmake ninja cjson node cppcheck
```

<a id="linux"></a>

Ubuntu 24.04 (the reference host version):

```sh
sudo apt-get update
sudo apt-get install git python3 python3-pip python3-venv cmake ninja-build \
  wget flex bison gperf ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 \
  g++ libcjson-dev curl libdigest-sha-perl
```

Node.js and Cppcheck come from the same package managers as the rest of the
host tools; the project deliberately keeps no version pin for either, so a
current release installed on the machine is what every script uses. Check
`python3 --version`, `cmake --version`,
`ninja --version`, and `c++ --version`; CMake must be **3.25 or newer** to read
the repository's version-6 presets. If a distribution package is older, upgrade
that tool before continuing. Debian and other Ubuntu releases can work, but
their package versions must pass these checks; Ubuntu 24.04 matches the
reference host.

| Dependency | Local version contract | Source of truth |
| --- | --- | --- |
| Node.js | No pin; any current release resolved from `PATH` | [`./scripts/dev doctor`](../scripts/dev) |
| Web UI packages | Exact direct and resolved versions via `npm ci` | [`package.json`](../package.json), [`package-lock.json`](../package-lock.json) |
| ESP-IDF | Exactly 6.1.0 (tag v6.1) | This guide and the build scripts |
| IDF components | Locked graph, including mDNS 1.13.1 | [`idf/dependencies.lock`](../idf/dependencies.lock) |
| Cppcheck | No pin; any current release resolved from `PATH` | [`./scripts/dev doctor`](../scripts/dev) |
| Host CMake | 3.25 or newer | [`CMakePresets.json`](../CMakePresets.json) schema 6 |
| Home Assistant tests (optional) | Python 3.14.2 or newer; dependencies locked with uv | [`pyproject.toml`](../integrations/OpenBrewByWeight/pyproject.toml), [`uv.lock`](../integrations/OpenBrewByWeight/uv.lock) |
| Home Assistant service (optional) | No running service for tests; integration test dependency is 2026.9.x | [Integration project](../integrations/OpenBrewByWeight/pyproject.toml) |

Git, Node.js, Cppcheck, the host compiler, Ninja, cJSON and the system Python
have no separate
project pin; use versions compatible with the requirements above and verify
them with the tests below. ESP-IDF manages its own Python environment. The
[Home Assistant integration](../integrations/OpenBrewByWeight/README.md) has a
separate Python and `uv.lock` contract; it is not needed for firmware builds.

<a id="2-nodejs-dependencies"></a>

Install the pinned Web UI dependencies from the repository root:

```sh
npm ci
```

This is an explicit setup step. Test commands never install dependencies.

## 3. Install ESP-IDF

<a id="3-install-esp-idf-required"></a>

Prefer the [ESP-IDF Installation Manager (EIM)](https://docs.espressif.com/projects/idf-im-ui/en/latest/):
install ESP-IDF **6.1.0** (tag **v6.1**), open a fresh shell, and source
the activation script printed by EIM (or use **Open IDF Terminal** in its GUI).
For example, use the actual filename EIM created:

```sh
source "$HOME/.espressif/tools/activate_idf_v6.1.sh"
idf.py --version
```

The project scripts reuse an active environment only when `IDF_PATH`, its
`IDF_PYTHON_ENV_PATH/bin/python`, `idf.py`, and the reported 6.1.0 version all
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
Every path rejects versions other than 6.1.0.

`idf/main/idf_component.yml` and `idf/dependencies.lock` define the component
graph, including mDNS 1.13.1. The build fails if dependency resolution changes
the lockfile; review and commit such updates separately. First firmware builds
may need network access to resolve SDK components; prepare these dependencies
before attempting an offline validation run. Host tests and firmware compilation
need no running Home Assistant service or connected controller.

### Upgrade dependencies deliberately

Run `npm ci` and `uv sync --locked` for ordinary setup; neither updates its
lockfile. To upgrade a Web UI package, choose its version with
`npm install --save-dev --save-exact <package>@<version>` and review both npm
files. To upgrade a Home Assistant test dependency, update `uv.lock` explicitly
with `uv lock --upgrade-package <package>` and review the resolved graph.
An ESP-IDF upgrade must change the local version guard and the generated
component lock together; regenerate the lock with the intended
SDK version, then run the full build gate. Do not hand-edit lockfiles.

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

### Optional: Home Assistant integration tests

This is a separate Python environment; it is not required for firmware or Web
UI checks. Install a current uv with its
[installer](https://docs.astral.sh/uv/getting-started/installation/)
or a version manager, then run from the integration directory:

```sh
cd integrations/OpenBrewByWeight
uv --version               # any current uv
uv python install 3.14
uv sync --locked --group test
uv run --no-sync python --version    # Python 3.14.x
uv run --no-sync pytest
uv run --no-sync ruff check .
uv run --no-sync mypy
cd ../..
```

The integration's `uv.lock` fixes package resolution, `--locked` rejects a
stale lockfile, and `--no-sync` prevents test commands from changing the
environment. These
checks do not require a running Home Assistant instance or controller.

<a id="8-host-tests-before-you-flash"></a>
<a id="4-ble-backend"></a>

## 5. Build

Prefer named hardware and machine profiles. A hardware profile selects the
assembled board, memory layout, GPIO wiring, relay polarity, and installed
peripherals; a machine profile selects its control topology and factory
defaults. See [Hardware and machine build profiles](BUILD_PROFILES.md) for the
complete contracts and compatibility rules. The reference combination, used in
the examples throughout this guide, is:

```sh
./scripts/dev build \
  --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra
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
settings survive ordinary boot and OTA. The current settings blob is schema 2;
same-layout schema-1 settings upgrade without losing saved values. The shot store
uses schema 2 and the curve store uses schema 2. Other incompatible settings are
rejected and replaced with factory defaults. Firmware identity is checked before
writing the device. Older incompatible partition layouts require the clean USB
installation described below; compatible OTA updates remain supported.

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
  --machine la-marzocco-linea-micra \
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
  --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra \
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

Supported ESP-IDF firmware builds default to `CONFIG_COMPILER_OPTIMIZATION_PERF=y`
from `idf/sdkconfig.defaults`, which selects GCC `-O2` for every built-in
hardware and machine profile. After compiling, the build verifier prints the
selected optimization level (`sdkconfig: CONFIG_COMPILER_OPTIMIZATION_PERF=y`
on default builds) and fails if the configuration does not hold it.
The `Debug` setting in the root `CMakePresets.json` applies only to host tests;
it does not change firmware optimization.

ESP-IDF offers exactly four optimization levels; `-O1` and `-O3` are not
selectable and the wrapper rejects them. To compile an experiment at another
level, pass exactly one of the transient build options; omitting them keeps
the default `-O2` build:

```sh
./scripts/dev build --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra
```

| Option | GCC level | Kconfig choice |
| --- | --- | --- |
| `--o0` | `-O0` | `CONFIG_COMPILER_OPTIMIZATION_NONE` |
| `--og` | `-Og` | `CONFIG_COMPILER_OPTIMIZATION_DEBUG` |
| `--o2` | `-O2` | `CONFIG_COMPILER_OPTIMIZATION_PERF` (default) |
| `--os` | `-Os` | `CONFIG_COMPILER_OPTIMIZATION_SIZE` |

The options are mutually exclusive, apply to that invocation only, and are
never persisted. They work through a generated defaults file appended to
`SDKCONFIG_DEFAULTS`; when an existing sdkconfig holds a different level, the
build recreates the configuration so the requested level takes effect, which
discards local `menuconfig` choices stored in that file. A later
build without a level option restores `-O2` the same way.
The current n16r8 image and memory baselines were measured with `-O2` and
apply to `--development -O2` builds. The historical n8r4 baselines retain
their `--development -Os` scope. Normal release builds and other optimization
levels still check the OTA partition limit, external BSS ceiling, and required
internal-memory placement; their image sizes do not establish a versioned
resource baseline.

The final verifier also rejects drift from the qualified production profile:
n8r4 uses 8 MB flash, `partitions-n8r4.csv`, and QUAD PSRAM; n16r8 uses 16 MB
flash, `partitions-n16r8.csv`, and OCT PSRAM. Both require DIO/80 MHz flash,
80 MHz PSRAM, a 32 KiB internal reserve, a 2 KiB malloc threshold above which
`malloc()` prefers PSRAM, 64 KiB MMU pages, rollback support,
mDNS dynamic responder allocations in PSRAM — the mDNS task stack follows the
variant: PSRAM on n8r4, internal RAM on n16r8 because flash core dumps cannot
read a stack in cache-backed PSRAM — the pinned boot/task/interrupt watchdog
and panic settings, and the GPTimer ISR handler in IRAM. Other application,
NimBLE/VHCI, HTTP, persistence, control, and flash-writing stacks remain
internal. These checks verify current hardware settings; they do not retune
clocks, partitions, or watchdog durations.

N16R8 builds map flash code and read-only data into PSRAM at startup and
execute from there, reserving roughly 1.9 MiB of PSRAM ahead of the heap.
N8R4 builds keep this option disabled. Flash operations still disable both
caches while they run, so this mapping does not change which code may run
during a write. An existing N16R8 build tree is regenerated when its selected
defaults change. A successful build verifies configuration and memory limits;
only an on-device comparison can establish whether loop gaps improve.

Each build compares its selected repository defaults and build profile with the
last verified build of that variant. If an input changed, was added, or was
removed, it regenerates `sdkconfig` and checks the effective configuration
before accepting the firmware image. An unchanged variant keeps its existing
configuration for incremental builds. Regeneration discards local `menuconfig`
choices in that variant, so record any custom choices before changing defaults.
Inspect one current tree with:

```sh
grep '^CONFIG_COMPILER_OPTIMIZATION' \
  build-idf/esp32-s3-relay-x1-speaker--rancilio-silvia-pro-x/sdkconfig
```

To restore repository defaults manually after changing `menuconfig`, rebuild
the selected variant with `--force-sdkconfig-regenerate`. This discards its
local `menuconfig` choices without cleaning the whole build tree:

```sh
./scripts/dev build --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra --force-sdkconfig-regenerate
```

Use the directory produced by the exact selected pair. The regeneration flag
applies only to commands that include `build`; standalone flash, OTA, and
monitor reject it. Do not add optimization
flags through `--flags`; the supported optimization contract is the Kconfig
selection above, which the build verifier checks. The `menuconfig` route for
the optimization level is gone: the wrapper recreates the configuration
whenever the level differs from the requested one.

Other choices in an existing IDF `sdkconfig` are retained until selected build
inputs change or regeneration is requested. A mismatch in a required production
setting stops the build and identifies the setting; use the regeneration flag
to restore defaults after a local `menuconfig` edit.
Omitting a macro does not always mean its feature is off; explicit flags
override matching choices. Review Diagnostic build identity and options before
installation.

The build renders the selected catalog into
`src/OpenBrewByWeightWebAssetsGzip.h`, generates version identity and
`build-idf/<hardware-id>--<machine-id>/openbrewbyweight.bin`, then checks image and
memory budgets. Use only the image for the intended profile pair. The supported partition layouts have
two app slots; arbitrary 4 MB layouts cannot hold this firmware.

The n16r8 layout reserves 640 KiB for a temporary ESP-IDF core dump and
1,408 KiB for two saved crash records. Its unused FFAT area is 7,128 KiB
(`0x6F6000` bytes). Both layouts reserve 816 KiB for shot curves. The n8r4
layout keeps its existing 64 KiB core-dump
partition but does not enable persistent crash capture. On n16r8, the mDNS
task stack is internal so that stack remains available to a core dump.

Build the supported built-in pairs listed in
[Build profiles](BUILD_PROFILES.md#capability-matching) with
`./scripts/dev build`. Release images disable the JTAG console and remote
machine control at compile time; each build leaves its named `.bin` and
archive under `build-idf/` and `artifacts/firmware/`.

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
  --hardware esp32-s3-relay-x1-speaker --machine la-marzocco-linea-micra
```

To build and install the exact result in one command, put the stages before
their options:

```sh
./scripts/dev build flash --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra \
  --port /dev/cu.usbmodem2101
```

Add `monitor` to open the serial console only after both earlier stages pass:

```sh
./scripts/dev build flash monitor --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra \
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

### Partition-layout updates

### Curve-layout transition

The timestamped curve format requires a one-time USB installation of the full
project build. Build for your hardware and machine, then use the ordinary USB
flash command without `--erase-all`:

```sh
./scripts/dev flash --confirm --port /dev/cu.usbmodem2101 \
  --hardware esp32-s3-relay-x1-speaker --machine la-marzocco-linea-micra
```

For the exact preceding project layout, the installer reports that old curves
will start empty and erases only the new curve region. It preserves settings,
Wi-Fi credentials, presets, calibration, shot summaries, activation history,
both application slots and crash storage. This consumes previously unused,
unmounted filesystem space. After this installation, ordinary OTA updates
remain available. App-only `--image` and OTA cannot change the partition table.
Any other incompatible layout is rejected; review its data-loss consequences
before explicitly choosing a clean installation.

### Other clean-install cutovers

The current settings contract is schema 2 and preserves same-layout schema-1
settings on upgrade; that upgrade does not require `--erase-all`. Other
incompatible settings contracts require a clean installation. An installed 20 KiB NVS layout,
or a current layout without the dedicated
816 KiB `shotcurve` partition or the 32 KiB `shotlog` and `history` partitions,
or an n16r8 layout without the new crash-history area,
needs a one-time clean USB installation. A normal flash refuses an incompatible
layout. **The following erases both firmware
slots and all saved data:** settings, Wi-Fi credentials and password, recipes
and presets, calibration, scale preferences, shot history and last shot. There
are also no retained crash records after this installation. Settings and
curves start from factory defaults without migration.

```sh
./scripts/dev flash --confirm --port /dev/cu.usbmodem2101 \
  --hardware esp32-s3-relay-x1-speaker --machine la-marzocco-linea-micra \
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

## 8. Update over Wi-Fi (OTA)

After installation, follow [OTA](features/ota.md) for upload, verification,
commit, confirmation and recovery. OTA cannot migrate the partition table.

Build and update the same profile in one command:

```sh
./scripts/dev build ota --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra \
  --host 192.168.1.50 \
  --yes --wait-for-confirmation
```

`--yes` accepts the commit question; `--wait-for-confirmation` independently
verifies the rebooted image. See the [complete `dev` examples](SCRIPTS.md) for
standalone and combined build, flash, OTA, and monitor commands.

## One supported command interface

`./scripts/dev` is the only supported public firmware command. The old direct
IDF stage aliases and combination wrappers were removed. Focused shell files
under `scripts/internal/` are private implementation details.

## Optional static analysis

Use [Static analysis](STATIC_ANALYSIS.md) when requested or required by the
validation gate. It lists prerequisites, tool behavior and failure meanings.

## Firmware version

`VERSION` supplies the release number; the build adds the git revision and a
dirty marker when applicable, and rejects an absent or invalid commit ID
instead of producing `unknown`. The Web UI footer and boot output
identify the resulting version.
