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

## Signal header design preview

To review the Wi-Fi and Bluetooth icon proposal on the existing Home layout,
run this command from the repository root with Node.js installed:

```sh
node scripts/preview_web_ui.js
```

Open `http://127.0.0.1:4173/` in a browser, or use
`http://127.0.0.1:4173/compare` for the original icon pair and five alternatives
in separate rows, each with desktop and mobile views side by side. Option 1
shows the current refinement: unchanged Wi-Fi arcs, a smaller Bluetooth symbol,
more prominent signal bars and tighter spacing. The five earlier icon
alternatives remain available for reference;
open `http://127.0.0.1:4173/?option=2` to review any option individually (1–6).
Each proposal also includes the scale disconnected while Wi-Fi stays connected.
Add `&scale=disconnected` to an individual option URL to inspect that state;
the current scale weight and timer become unavailable, while the last shot remains visible.
The server listens only on this
computer and reads the current Home markup and styles on each page load;
refresh after editing the proposal files in `scripts/web-preview/`.
Keep the process running while iterating, and stop it with Ctrl-C when finished.
If the port is already occupied, use `PORT=4174 node scripts/preview_web_ui.js`.

Each header icon opens its own popup: Wi-Fi shows the network name and signal;
Bluetooth shows the scale name and signal. The preview uses the firmware's
popup markup and interaction logic with simulated values.
Expand **Proposal 01 · simulated
signals** (numbered for the selected option) to compare strong, medium, weak and disconnected states, or change
the theme. Resize the browser below 700 pixels to inspect the mobile header.
Home values and signal readings are illustrative; navigation and switches are
presentation-only. This preview does not connect to a device, change saved
settings, or ship in firmware assets.

### Mobile navigation preview

Open `http://127.0.0.1:4173/mobile-menu` on the same preview server to review
the bottom navigation proposal. Below 700 pixels, Home, Stats, History and
Settings appear in a fixed bottom bar with icons and a highlighted current tab.
The header's **More options** button (three dots) opens a compact menu for
Diagnostics and Admin. The bottom tabs stay visible while that menu is open.
Tap outside, press Escape or select a destination to close it. With a keyboard,
press Down Arrow on the button to focus the menu, then Tab through its links.
On wider screens, the existing top navigation remains visible.

On mobile, scrolling gradually compacts the header while keeping the smaller logo,
connection indicators and More options button visible. Return to the top to restore
the full-size logo.

The tabs open sample views; browser Back and Forward also work. Expand
**Mobile navigation proposal · sample data** to change the theme or show and
hide Diagnostics. The bar reserves room for the phone's safe area and the
page content. Settings and device actions are disabled in this proposal;
it does not connect to hardware or change firmware.

### No-scale guard icon preview

Open `http://127.0.0.1:4173/compare#no-scale` on the same running server
to review the simplified X-only icon used for aborted no-scale guard attempts.
Its visible size matches the other history icons, with rounded ends. Enlarged views and
24, 32 and 48 pixel samples show it on light and dark backgrounds.
The preview does not connect to a device or change its settings.

## USB installation

Review the selected profiles and physical safety before adding `--confirm`.
The flag authorizes hardware access; it does not skip image, profile, or
partition checks. The exact previous project layout supports a preserving
[USB curve-layout transition](BUILD.md#curve-layout-transition) with the normal
flash command. It starts old curves empty while retaining settings and other
history; subsequent updates can use OTA. Use `--erase-all` only for an explicitly
chosen clean installation:

```sh
./scripts/dev build flash --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine la-marzocco-linea-micra \
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
  --machine la-marzocco-linea-micra \
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
scripts never install them automatically. `doctor` also reports each tool's
resolved path and version and warns when a Homebrew-managed tool is outdated,
so checks always run against the machine's current releases.

Node.js and Cppcheck carry no project version pin: every script resolves them
from `PATH`, so install current releases with your package manager (see
[Build](BUILD.md#2-install-host-prerequisites)). `./scripts/dev analyze` needs
Cppcheck on `PATH`; see [Static analysis](STATIC_ANALYSIS.md) for local setup.
Firmware builds require ESP-IDF 6.1.0 and fail if the IDF component lock
changes during resolution. Review upgrades through [Build](BUILD.md#upgrade-dependencies-deliberately).

To regenerate the immutable time-zone table after reviewing a new IANA
release, place the official `tzdata2026d.tar.gz` archive in
`temp/ai_temp_timezone_impl/` and run:

```sh
python3 scripts/generate_timezones.py \
  temp/ai_temp_timezone_impl/tzdata2026d.tar.gz \
  src/ShotStopperTimeZoneData.h
```

The script checks the pinned archive SHA-256 and release version, uses the
local `zic` compiler, and writes the bounded 2025–2099 table. It never
downloads data. Review the generated diff and run the normal validation gate.
For a future release, update the pinned version and hash deliberately along
with boundary fixtures and resource measurements.

For a failed boot or a changed partition layout, reinstall the complete image
over USB with `--erase-all` and follow [Emergency recovery](EMERGENCY_RECOVERY.md).
