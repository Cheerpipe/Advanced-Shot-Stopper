# Build, test, and install

<a id="build-environment"></a>

Supported firmware uses ESP-IDF 6.1.x (pinned reference: **6.1**),
Arduino-ESP32 **3.3.11** as an IDF component, and native NimBLE.
The bundled EspressoScaleBLE library is not installed through Library Manager.

This walkthrough covers macOS and Debian/Ubuntu Linux. Native Windows build
and hardware-installation steps are not qualified here; the Windows notes in
[Static analysis](STATIC_ANALYSIS.md#4-windows-prerequisites-native-no-wsl)
cover tool preparation only.

Read [Hardware](HARDWARE.md) before connecting equipment. Building and host tests
do not need a connected controller. Flash/OTA sections explicitly affect it.

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

Use a separate SDK directory. This subshell returns you to the repository
when installation finishes:

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
over it. For an SDK elsewhere, export `IDF_PATH` and source its `export.sh`.
Build scripts can discover `IDF_PATH` or `$HOME/esp/esp-idf-v6.1` when needed.
They reject versions outside 6.1.x.

`idf/main/idf_component.yml` and `idf/dependencies.lock` pin the component
graph. First firmware builds may need network access to resolve SDK components;
prepare these dependencies before attempting an offline validation run.

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

<a id="8-host-tests-before-you-flash"></a>
<a id="4-ble-backend"></a>

## 5. Build

Choose the board from its module marking: **n16r8** means 16 MB flash / 8 MB
OPI PSRAM; **n8r4** means 8 MB flash / 4 MB QSPI PSRAM. Neither selects GPIOs
for an arbitrary third-party board.

From the repository root:

```sh
./scripts/dev build --arch n16r8
```

The wrapper preserves project diagnostics but hides ESP-IDF 6.1's known
`esp_wifi`/`wpa_supplicant` component-validation warnings. Extra-warning
reports retain their unfiltered SDK log and report project-owned warnings
separately.

The facade passes empty extra flags unless supplied; it does not reuse a saved
extra-flags preference implicitly. The direct `build-idf` script instead
resolves flags from CLI, environment, saved values, or a prompt.

Set the machine type deliberately when changing it:

```sh
./scripts/dev build --arch n16r8 \
  --flags "-DSHOT_STOPPER_MACHINE_TYPE=2 -DSHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=0"
```

| Option | Meaning |
| --- | --- |
| `SHOT_STOPPER_MACHINE_TYPE=0/1/2` | Paddle / momentary / momentary+reed; see [machine types](../README.md#machine-types). |
| `SHOT_STOPPER_ENABLE_BUZZER=0/1` | Omit / include local passive buzzer. IDF Kconfig defaults to 1; explicitly use 0 when absent. |
| `SHOT_STOPPER_ENABLE_JTAG=1` | Development USB Serial/JTAG at boot without the GPIO4 console jumper. |
| `SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=0/1` | Remote start/rinse disabled / explicit opt-in. Default is disabled. |
| `SHOT_STOPPER_DEVELOPMENT=1` | Bypasses Admin unlock for local development only. Never use for an installed machine. |

### Compiler optimization and existing sdkconfig

Supported ESP-IDF firmware builds use `CONFIG_COMPILER_OPTIMIZATION_PERF=y`
from `idf/sdkconfig.defaults`, which selects GCC `-O2` for both supported
architectures. After compiling, the build verifier prints
`sdkconfig: CONFIG_COMPILER_OPTIMIZATION_PERF=y (-O2)` and fails if the
architecture-specific configuration selects another optimization level.
The `Debug` setting in the root `CMakePresets.json` applies only to host tests;
it does not change firmware optimization.

Defaults seed a new `build-idf/<architecture>/sdkconfig`; they do not overwrite
an existing file. Inspect an existing n16r8 tree with:

```sh
grep '^CONFIG_COMPILER_OPTIMIZATION' build-idf/n16r8/sdkconfig
```

To restore all repository defaults for that architecture, remove its generated
configuration and rebuild. This also discards every other local `menuconfig`
change stored in that file:

```sh
rm build-idf/n16r8/sdkconfig
./scripts/dev build --arch n16r8
```

To preserve other local choices, first load the ESP-IDF environment described
in section 3, then change only **Compiler options → Optimization Level** to
**Optimize for performance (-O2)** and rebuild through the project wrapper:

```sh
idf.py -C idf -B build-idf/n16r8 menuconfig
./scripts/dev build --arch n16r8
```

Replace `n16r8` with `n8r4` in every command when working on that architecture.
Do not add `-O2` through `--flags`; the supported optimization contract is the
Kconfig selection above, which the build verifier checks.

Other choices in an existing IDF `sdkconfig` are retained in the same way.
Omitting a macro does not always mean its feature is off; explicit flags
override matching choices. Review Diagnostic build identity and options before
installation.

The build generates Web assets, version identity and
`build-idf/<arch>/shotstopper.bin`, then checks image and memory budgets.
Use only the image for your architecture. The supported partition layouts have
two app slots; arbitrary 4 MB layouts cannot hold this firmware.

GitHub Actions publishes six production OTA variants: `n8r4` and `n16r8`, each
for `paddle-latch`, `momentary`, and `momentary-reed`. Their names follow
`shotstopper-ota-<arch>-<machine>-jtag-off-remote-off.bin`; those two features
are explicitly disabled at compile time. GitHub downloads each artifact as a
ZIP container, but that container holds only the named OTA-ready `.bin` file.

## 6. Flash (USB)

Proceed only after reviewing the image, board, wiring and applicable
[bench checks](MANUAL_TEST_PLAN.md). Keep the machine activation circuit
disconnected for initial verification.

Connect a USB **data** cable. Use BOOT + RST for ROM download if the app exposes
no port. App serial monitoring needs the
[GPIO4 jumper or a JTAG build](HARDWARE.md#usb-console-jumper).

Replace the port below with the detected controller port:

```sh
./scripts/dev flash --confirm --port /dev/cu.usbmodem2101 --arch n16r8
```

Linux commonly uses `/dev/ttyACM0` or `/dev/ttyUSB0`. The direct installer
can rebuild a stale build before flashing; successful transfer still requires
post-boot and electrical verification.

### Legacy partition migration

An installed 20 KiB NVS layout needs a one-time clean USB migration to 84 KiB.
A normal flash refuses that layout. **The following erases both firmware slots
and all saved data:** Wi-Fi, password, recipes, calibration, scale preferences,
history and last shot. There is no automatic data migration.

```sh
./scripts/dev flash --confirm --port /dev/cu.usbmodem2101 --arch n16r8 --erase-all
```

Record the settings you need before migrating. Do not combine `--erase-all`
with an external `--image`; it requires the project's full build outputs.

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

## Compatibility aliases

`build`, `flash`, `monitor`, `ota`, `static`, `bf`, `bfm`, `bo`,
and `bsfm` are ESP-IDF compatibility aliases, not Arduino-cli workflows.
Use [SCRIPTS.md](SCRIPTS.md) for parameter resolution and direct wrappers.

## Optional static analysis

Use [Static analysis](STATIC_ANALYSIS.md) when requested or required by the
validation gate. It lists prerequisites, tool behavior and failure meanings.

## Firmware version

`VERSION` supplies the release number; the build adds the git revision and a
dirty marker when applicable. The Web UI footer and boot output identify it.
