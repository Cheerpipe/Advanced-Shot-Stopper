# Script reference

`./scripts/dev` is the only supported public firmware command. It never
installs dependencies or contacts hardware unless a command explicitly asks to
flash over USB.

## Commands

| Task | Command |
| --- | --- |
| Inspect profile compatibility | `./scripts/dev profiles` |
| Build firmware | `./scripts/dev build --hardware <hardware> --machine <machine>` |
| Build and install over USB | `./scripts/dev build flash --confirm --hardware <hardware> --machine <machine> --port <port>` |
| Flash an existing image | `./scripts/dev flash --confirm --hardware <hardware> --machine <machine> --port <port>` |
| Monitor USB serial | `./scripts/dev monitor --port <port> --speed 115200` |
| Run host tests | `./scripts/dev test normal` |
| Validate changes | `./scripts/dev validate` |

Successful firmware builds automatically retain ELF/BIN pairs in
`artifacts/firmware/<hardware>--<machine>/<ELF SHA>/`, including `build` without
`flash`. The directory also contains image identity for matching a crash log to
its ELF; `./scripts/dev clean` keeps these archives.

At the end of every successful command that includes `build`, the terminal
highlights the absolute build-output folder and the absolute `shotstopper.bin`
path ready for installation.

## USB installation

Review the selected profiles and physical safety before adding `--confirm`.
The flag authorizes hardware access; it does not skip image, profile, or
partition checks. Use `--erase-all` for a clean schema-1 installation:

```sh
./scripts/dev build flash --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --port /dev/cu.usbmodem2101 \
  --erase-all
```

The clean install erases both firmware slots and all persisted data: settings,
Wi-Fi credentials, presets, scale preferences, shot history, curves, and the
last-shot record. Record anything needed before running it.

`--image <path>` flashes an existing verified project image and is rejected
with `build`. `--no-check` skips only local image identity verification; it
does not skip partition checks. `--port` must name a connected USB serial
device. On macOS use `/dev/cu.usbmodem*`; on Linux use `/dev/ttyACM*` or
`/dev/ttyUSB*`.

## Wi-Fi update (OTA)

Upload a built image to a controller on the same network without a USB cable:

```sh
./scripts/dev build ota --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --host 192.168.1.50 \
  --yes --wait-for-confirmation
```

`--host` accepts the controller's station address, or `192.168.4.1` on its
access point. The hidden prompt, or `SHOTSTOPPER_DEVICE_PASSWORD`, supplies
the device password; commands never take it as an argument. `--image <path>`
uploads an existing image instead of building. `--yes` answers the commit
question; `--wait-for-confirmation` independently verifies the rebooted image.
Transfers are resumable: an interrupted run queries the confirmed offset and
repeats only the missing ranges. Safety behavior, session details, and
troubleshooting: [OTA](features/ota.md).

## Build profiles

Every project build requires an exact hardware and machine profile. Use
`./scripts/dev profiles` to inspect built-in profiles and physically compatible
pairs. The compatible built-in pairs are listed in
[Build profiles](BUILD_PROFILES.md#capability-matching); each machine profile
declares its compatible hardware profiles. The optional `--development`
profile enables the development admin/JTAG settings; `--release` is the
default. These options are compile-time only and are never persisted.

`./scripts/dev validate --risk R2` builds each official validation pair;
`--risk R3` also checks each pair with Cppcheck and a GCC warning build.

The supported transient options are `--webui-language`, `--flags`, `--o0`,
`--og`, `--o2`, and `--os`. The build also accepts
`--force-sdkconfig-regenerate` to recreate its selected variant's configuration
from repository defaults without cleaning the build tree. Builds do this
automatically when selected defaults change or a legacy tree has no recorded
input fingerprint. Regeneration discards local `menuconfig` choices; an
unchanged variant retains them. The flag is rejected for standalone flash, OTA,
and monitor. The stored device password is never accepted on a
command line; use the hidden prompt or the documented USB serial procedure.
The current n16r8 resource baseline applies to the default `-O2` builds;
the historical n8r4 baseline applies to `-Os`. Other optimization levels
retain the physical image and memory placement checks; see [Build](BUILD.md#compiler-optimization-and-existing-sdkconfig).

## Diagnostics and recovery

Use `./scripts/dev doctor` to inspect local tooling and
`./scripts/dev context <area>` for focused repository paths. Use
`./scripts/dev classify` before a change and `./scripts/dev validate` for the
required validation gate. Missing dependencies are reported as failures;
scripts never install them automatically.

For a failed boot or a changed partition layout, reinstall the complete image
over USB with `--erase-all` and follow [Emergency recovery](EMERGENCY_RECOVERY.md).
