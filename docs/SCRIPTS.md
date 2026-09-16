# The `dev` command

`./scripts/dev` is the single supported entry point for building, installing,
updating, monitoring, testing, and validating Shot Stopper. Run it from the
repository root. It writes complete logs and a redacted JSON summary under
`artifacts/runs/`.

```sh
./scripts/dev --help
./scripts/dev build --help
```

The shell files under `scripts/internal/` are implementation details. Do not
call them directly: only `dev` provides confirmation checks, secure stdin
handling, ordered pipelines, consistent logging, and the supported public
contract.

These commands are different from firmware commands such as `HELP`,
`NET_STATUS`, and `FACTORY_RESET`. Type those inside the
[USB serial CLI](SERIAL_CLI.md), not in the shell.

## Everyday commands

| Task | Command |
| --- | --- |
| Show available hardware/machine profiles | `./scripts/dev profiles` |
| Build firmware | `./scripts/dev build --hardware <hardware> --machine <machine>` |
| Build and install over USB | `./scripts/dev build flash --confirm --hardware <hardware> --machine <machine>` |
| Build, install, and monitor | `./scripts/dev build flash monitor --confirm --hardware <hardware> --machine <machine>` |
| Build and update over Wi-Fi | `./scripts/dev build ota --confirm --hardware <hardware> --machine <machine> --host <host>` |
| Flash an existing build | `./scripts/dev flash --confirm --hardware <hardware> --machine <machine>` |
| Update an existing build over Wi-Fi | `./scripts/dev ota --confirm --hardware <hardware> --machine <machine> --host <host>` |
| Open only the serial monitor | `./scripts/dev monitor` |
| Classify and validate changes | `./scripts/dev classify` / `./scripts/dev validate` |

Flash and OTA affect a controller. Review the selected profiles and physical
safety before adding `--confirm`. The flag authorizes the operation; it does
not skip image, profile, partition, or OTA safety checks.

## Firmware pipelines

Firmware stages are written in execution order immediately after `dev`:

```text
build -> flash or ota -> monitor
```

The supported sequences are:

| Sequence | Result |
| --- | --- |
| `build` | Compile without contacting hardware. |
| `flash` | Install an existing compatible image over USB. |
| `ota` | Upload an existing compatible image over Wi-Fi. |
| `monitor` | Open the USB serial monitor. |
| `build flash` | Compile, then USB-install that exact build. |
| `build ota` | Compile, then OTA-upload that exact build. |
| `flash monitor` | USB-install, then monitor the same serial target. |
| `ota monitor` | OTA-upload, then open a separately resolved local USB port. |
| `build flash monitor` | Compile, USB-install, then monitor. |
| `build ota monitor` | Compile, OTA-upload, then monitor through local USB. |

`flash` and `ota` cannot appear together, stages cannot repeat, and their order
cannot change. `build monitor` is intentionally rejected because it could imply
that the new build was installed; run the two commands separately when that is
really what you want. Put every option after the complete sequence:

```sh
# Correct
./scripts/dev build flash monitor --confirm --hardware <hardware> --machine <machine>

# Rejected: a stage appears after options
./scripts/dev build --hardware <hardware> flash --machine <machine>
```

The pipeline stops on the first failure. Monitor never starts after a failed
build, flash, or OTA. A combined build does not accept `--image`; its transfer
stage always consumes the artifact produced for the same profile pair.

## Parameter resolution and remembered defaults

Required values resolve in this order:

1. A named command-line option.
2. Its environment variable.
3. `.shotstopper` at the repository root.
4. An interactive prompt.

The whole pipeline resolves its required values before the first stage. This
means a missing USB port or OTA host is reported before a long build begins.
In a terminal, Enter accepts the suggested value. In CI, a pipe, or with
`SHOTSTOPPER_NONINTERACTIVE=1`, missing values are errors instead of prompts.

The following ordinary values may be remembered: port, architecture, monitor
speed, OTA host, and extra compiler flags. A missing or stale serial path is
forgotten before use. Hardware and machine profiles, image paths, build/report
overrides, Web UI language, development mode, confirmation options, and OTA
one-shot controls are never persisted.

The device password is never read from `.shotstopper`, displayed as a default,
written to logs, or persisted. `.shotstopper` is Git-ignored and created with
mode `600`.

### USB port discovery

When a flash or monitor pipeline needs a port, `dev` checks the explicit or
remembered path. If it is missing, the interactive prompt lists USB-CDC devices
matching `/dev/cu.usbmodem*` on macOS or `/dev/ttyACM*` on Linux and suggests
the first match. It never silently selects a device; press Enter to accept the
suggestion or type another path.

App CDC requires the [GPIO 4 console jumper](HARDWARE.md#usb-console-jumper) at
reset unless the firmware was built with `SHOT_STOPPER_ENABLE_JTAG=1`. ROM
download mode through BOOT + RST can still expose a flashing port without that
jumper. A default build therefore has no console output for the monitor stage,
so `dev` refuses a pipeline that combines `build` with `monitor` unless that
JTAG build is requested: pass `--jtag` (or include
`-DSHOT_STOPPER_ENABLE_JTAG=1` in `--flags`) or run monitor separately.

## Options

| Option | Environment | Applies to | Meaning |
| --- | --- | --- | --- |
| `--hardware <id-or-json>` | `SHOTSTOPPER_HARDWARE` | all profile-aware stages | Exact built-in hardware ID or JSON path. Pair with `--machine`; never persisted. |
| `--machine <id-or-json>` | `SHOTSTOPPER_MACHINE` | all profile-aware stages | Exact built-in machine ID or JSON path. Pair with `--hardware`; never persisted. |
| `-a`, `--arch <arch>` | `SHOTSTOPPER_ARCH` | legacy image, monitor, analysis | `n8r4` or `n16r8`. Builds derive it from hardware; it may only confirm that result. |
| `-f`, `--flags "<flags>"` | `SHOTSTOPPER_FLAGS` | build | Extra compile definitions/options as one shell argument. |
| `--development` | — | build | Development-mode build for this invocation only. Never persisted. |
| `--jtag` | — | build | Compile the USB Serial/JTAG console on at boot by adding `-DSHOT_STOPPER_ENABLE_JTAG=1` on top of `--flags`. Never persisted. |
| `--webui-language <code>` | `SHOTSTOPPER_WEBUI_LANGUAGE` | build | Compile-time Web UI language; defaults to `en` and is never persisted. |
| `-p`, `--port <path>` | `SHOTSTOPPER_PORT` | flash, monitor | USB serial device. Validated before use and remembered. |
| `-s`, `--speed <baud>` | `SHOTSTOPPER_SPEED` | monitor | Monitor baud rate, normally `115200`; remembered. |
| `-H`, `--host <host>` | `SHOTSTOPPER_HOST` | OTA | Controller IP or hostname; remembered. |
| `-i`, `--image <file>` | `SHOTSTOPPER_IMAGE` | standalone flash or OTA | Existing `.bin`; verified and never persisted. Rejected with `build`. |
| `--no-check` | — | flash | Skip local image identity checking. It does not skip partition checks and is unavailable for OTA. |
| `--erase-all` | — | flash | Erase all flash before installing project outputs. Destructive and rejected with `--image`. |
| `--discard-ota-session` | — | OTA | Intentionally discard a different partial/staged remote image. |
| `--yes` | — | OTA | Accept the commit-and-reboot question without prompting. |
| `--wait-for-confirmation` | — | OTA | Poll after commit until the expected new image confirms or the existing timeout/error is reached. |
| `--password-stdin` | — | OTA | Read one password line from standard input. The value is passed internally through the environment. |
| `--confirm` | — | flash, OTA | Authorize a hardware-affecting pipeline at the public facade. |
| `--verbosity compact\|normal\|verbose` | `SHOTSTOPPER_VERBOSITY` | `dev` | Select facade output detail; independent of firmware logging. Place before the command. |

`--hardware-config` and `--machine-config` remain deprecated spellings for the
two profile selectors. `--force` has been removed because it coupled two
different OTA decisions. Use `--yes`, `--wait-for-confirmation`, or both.
Passwords in `--password`, `--token`, or `-t` are rejected because process
arguments and shell history can expose them.

## Build examples

List profile IDs before selecting a pair:

```sh
./scripts/dev profiles
```

Build for a Rancilio Silvia Pro X without reed feedback:

```sh
./scripts/dev build \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x
```

Build the reed-equipped variant:

```sh
./scripts/dev build \
  --hardware esp32-s3-relay-x1-speaker-reed \
  --machine rancilio-silvia-pro-x-reed
```

Build for a La Marzocco Linea Micra:

```sh
./scripts/dev build \
  --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra
```

Build with explicit compiler options:

```sh
./scripts/dev build \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --flags "-Werror=deprecated-copy"
```

Build a development image with the USB Serial/JTAG console on at boot (OpenOCD
plus the serial CLI without the GPIO 4 jumper). `--jtag` adds
`-DSHOT_STOPPER_ENABLE_JTAG=1` on top of any other flags; passing the same
define through `--flags` still works:

```sh
./scripts/dev build \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --jtag
```

Build a transient local-development image:

```sh
./scripts/dev build \
  --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra \
  --development
```

Explicit `--flags` are applied after JSON profile values. The resolver rejects
contradictory hardware/machine requirements, unsafe GPIO collisions, relay
configurations outside the supported safety contract, and incompatible
compile-time overrides. Development mode is CLI-only and must not be shipped.
See [build profiles](BUILD_PROFILES.md) for the complete JSON contract.

Extra flags follow the same CLI, environment, saved-value, and prompt
precedence. Pass `--flags=""` when you intentionally want an empty value and
want any remembered flags removed. Generated files live under
`build-idf/<hardware>--<machine>/`.

## USB flash examples

Flash a previously built profile image, specifying a macOS port:

```sh
./scripts/dev flash --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --port /dev/cu.usbmodem2101
```

On Linux:

```sh
./scripts/dev flash --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra \
  --port /dev/ttyACM0
```

Omit `--port` in an interactive terminal to validate the remembered path or
choose from detected USB-CDC ports:

```sh
./scripts/dev flash --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x
```

Build and flash in one operation:

```sh
./scripts/dev build flash --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x
```

Build, flash, and immediately monitor:

```sh
./scripts/dev build flash monitor --confirm \
  --hardware esp32-s3-relay-x1-speaker-reed \
  --machine rancilio-silvia-pro-x-reed \
  --port /dev/cu.usbmodem2101 \
  --speed 115200
```

Flash an existing build and monitor it without rebuilding:

```sh
./scripts/dev flash monitor --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra \
  --port /dev/ttyACM0 \
  --speed 115200
```

For a required partition-layout migration, `--erase-all` erases both firmware
slots, Wi-Fi, device password, all settings and presets, calibration, BLE
preferences, and shot history before writing the complete project image:

```sh
./scripts/dev build flash --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --port /dev/cu.usbmodem2101 \
  --erase-all
```

An external application image can be flashed only without `build`. It replaces
the application at the installed `app0` offset and does not reconstruct the
bootloader or partition table:

```sh
./scripts/dev flash --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --port /dev/cu.usbmodem2101 \
  --image /path/to/shotstopper.bin
```

## OTA examples

Build and update over Wi-Fi, entering the device password in the hidden prompt
and answering the commit question interactively:

```sh
./scripts/dev build ota --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --host 192.168.1.50
```

Automatically accept commit but return once it is accepted:

```sh
./scripts/dev build ota --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra \
  --host 192.168.1.50 \
  --yes
```

Ask before commit and then verify the new boot:

```sh
./scripts/dev ota --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --host 192.168.1.50 \
  --wait-for-confirmation
```

For unattended automation, provide the password through a secret environment,
accept commit, and require post-boot confirmation:

```sh
SHOTSTOPPER_DEVICE_PASSWORD="$DEVICE_SECRET" \
  ./scripts/dev build ota --confirm \
    --hardware esp32-s3-relay-x1-speaker \
    --machine rancilio-silvia-pro-x \
    --host 192.168.1.50 \
    --yes --wait-for-confirmation
```

Or stream one password line from a secret provider without placing the value in
the command arguments:

```sh
secret-provider-command | ./scripts/dev ota --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --host 192.168.1.50 \
  --password-stdin --yes --wait-for-confirmation
```

Use the controller SoftAP address when connected directly to it:

```sh
./scripts/dev ota --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --host 192.168.4.1 \
  --wait-for-confirmation
```

Resume a matching partial OTA by running the same command with the same image
and profiles. To intentionally replace a different remote session:

```sh
./scripts/dev ota --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --host 192.168.1.50 \
  --discard-ota-session --wait-for-confirmation
```

Upload an existing external image without rebuilding:

```sh
./scripts/dev ota --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --host 192.168.1.50 \
  --image /path/to/shotstopper.bin \
  --wait-for-confirmation
```

`--yes` and `--wait-for-confirmation` are independent:

| Options | Commit | Result after commit |
| --- | --- | --- |
| neither | Ask in the terminal | Return when commit is accepted. |
| `--yes` | Do not ask | Return when commit is accepted. |
| `--wait-for-confirmation` | Ask in the terminal | Poll for the expected confirmed boot. |
| both | Do not ask | Poll for the expected confirmed boot. |

A non-interactive OTA without `--yes` fails before creating or uploading a new
session because it cannot obtain commit consent. `--wait-for-confirmation`
checks for a changed boot ID, the expected image digest, and `confirmed: true`;
matching version text alone is not sufficient.

## OTA plus serial monitoring

OTA uses `--host` for the network controller. A following monitor stage uses a
local `--port`; these are independent endpoints. Use this only when the same
controller is also connected over USB:

```sh
./scripts/dev build ota monitor --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --host 192.168.1.50 \
  --port /dev/cu.usbmodem2101 \
  --speed 115200 \
  --yes --wait-for-confirmation
```

Without a build stage:

```sh
./scripts/dev ota monitor --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra \
  --host 192.168.1.50 \
  --port /dev/ttyACM0 \
  --speed 115200 \
  --wait-for-confirmation
```

## Monitor-only examples

Use remembered port and speed, prompting when either is unavailable:

```sh
./scripts/dev monitor
```

Specify both explicitly:

```sh
./scripts/dev monitor --port /dev/cu.usbmodem2101 --speed 115200
```

Add profiles so ESP-IDF uses that variant's ELF to decode backtraces:

```sh
./scripts/dev monitor \
  --hardware esp32-s3-relay-x1-speaker-reed \
  --machine rancilio-silvia-pro-x-reed \
  --port /dev/cu.usbmodem2101 \
  --speed 115200
```

Exit the ESP-IDF monitor with **Ctrl+]**.

## Development and validation commands

```sh
./scripts/dev context build
./scripts/dev classify
./scripts/dev test tooling
./scripts/dev test ota
./scripts/dev validate
./scripts/dev analyze --arch n16r8 \
  --build-dir build-idf/esp32-s3-relay-x1-speaker--rancilio-silvia-pro-x
```

Complete validation is selected by [VALIDATION.md](../VALIDATION.md), not by a
single convenient focused test. `dev` never installs dependencies. Missing
required tools produce exit code 127 instead of silently skipping work.

Advanced static-analysis helpers remain documented in
[Static analysis](STATIC_ANALYSIS.md); they are not alternative firmware build,
flash, OTA, or monitor entry points.

## Transfer details and recovery

USB flash validates the local identity unless `--no-check` is explicitly used,
then reads the installed partition table. It refuses incompatible NVS or
`shotcurve` layouts unless the destructive full-project `--erase-all` path is
selected. Both checked and unchecked project installs use existing ESP-IDF
`flash_args` and never rebuild during transfer.

OTA computes SHA-256, verifies architecture and profile identity, and resumes a
matching named session from the controller's validated offset. A different
partial/staged image is preserved unless `--discard-ota-session` is explicit.
Transient transport failures are reconciled with device status and retried only
within the bounded existing policy; safety and validation failures are not
retried. See [OTA](features/ota.md) for the controller-side safety, rollback,
and confirmation contract.
