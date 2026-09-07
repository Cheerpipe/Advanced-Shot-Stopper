# OTA

Image format and cross-client parser rules:
[OTA image identity contract](ota-image-identity.md).

Firmware can be updated over Wi-Fi without opening the case or using USB.
Upload a built image with the project scripts while the controller is on
your network.

## Requirements

- Pass the **device password** every CLI run (`--password` / `-t`, or
  `SHOTSTOPPER_DEVICE_PASSWORD`). Scripts never store it. The factory
  default (`ineedacoffee`) works if it was never changed.
- From **Admin → Firmware update**, unlock administration first. The Web UI
  does not ask for the device password again.
- Image must match the board architecture and must not be older than the
  running version (downgrades are refused).

## Safety behavior

The shot always has priority. The paddle is never blocked by an update.

- **No upload or flash during a shot.** Starting an update while the machine is pouring is refused (`CONFIG_LOCKED_DURING_ACTIVE_CYCLE`) so Wi-Fi does not compete with the scale.
- **Paddle during transfer aborts the upload** (`SAFETY_LOST`). The spare slot is discarded; the running firmware is untouched. A verified staged image is kept if you pull a shot *after* verify — flash waits until idle.
- **No planned restart during a shot.** Flash, Admin Restart, and serial `REBOOT` wait until the pour ends and the circuit is open. They do not open the relay to make way for the reset.
- Confirming or rolling back a `PENDING_VERIFY` image writes otadata (flash cache off). That write is deferred while a shot is pouring or GATT is up.

Updates use a dual slot. After reboot, the new image boots as
`PENDING_VERIFY` and is confirmed when HTTP is available and boot uptime is
at least 15 s. Opening a browser is not required. A connected scale can defer
confirmation until 180 s; an active cycle or closed relay always defers it.
A second OTA while verification is still pending is refused
(`PENDING_VERIFY`).

If HTTP never comes up, the controller waits up to 180 s, then:

- **Previous slot bootable:** the running image is marked invalid and
  the bootloader rolls back on restart — no USB required. That restart
  still waits if a shot is in progress.
- **No bootable previous slot:** the running image is confirmed so the
  machine is not left without an application. Recover over USB; see
  [Emergency recovery](../EMERGENCY_RECOVERY.md).

Watchdog and panic still open the circuit and reset immediately: a hung
firmware cannot wait for the shot to finish.

Wi-Fi credentials, presets, calibration, and shot history are left
unchanged on a successful flash.

## How to run

OTA can be performed from the Web UI or from the command line. In both cases,
the controller and the computer must be on the same Wi-Fi network (or the
controller's SoftAP network).

### Web UI

Open **Admin → Firmware update**, unlock Admin with the device password, then
select the firmware `.bin`. Choose **Upload and verify** and, once verification
succeeds, select **Flash and restart**. The controller IP is shown on the
Admin page; when using its SoftAP it is always `192.168.4.1`.

### Command line (CLI)

Use the project scripts from the repository root. The CLI authenticates with
the device password; pass it with `--password` (or `-t`) or enter it when the
script prompts. The password is never saved by the scripts.

The CLI computes the image SHA-256 before the transfer, creates a named OTA
session, and sends 64 KiB ranges. If Wi-Fi drops, it queries the confirmed
offset and repeats only the unconfirmed range; it never resends the complete
image. A later CLI invocation queries the controller first and adopts its
`transferId` when SHA-256, size, architecture, and version match. The device
keeps a checksummed, double-record journal every 512 KiB so the same transfer
can continue after a client or controller restart. A TCP cut keeps complete
4 KiB sectors; reboot can retreat to the last durable checkpoint and resend
its tail. Staged state is RAM-only: reboot after staging requires a new upload.
A different build is left untouched and
must be discarded explicitly with `--discard-ota-session` before a new session
can begin.

Success after reboot requires a changed `bootId`, the expected running image
digest and `confirmed: true`. Matching version strings alone are insufficient,
including reinstalling the same ROM. The Web retains the expected identity
across reloads and continues checking when Admin is unlocked again; it stores
no password. Older firmware without the evidence fields is reported as
unverified rather than successfully updated.

Session failures report the transport result, HTTP status, stable server error
code, and safe message. A controller with the immediately preceding resumable
schema can receive this upgrade. A controller that only exposes the older
single-POST protocol requires one USB update; the CLI never falls back to that
protocol silently.

`--no-check` is intentionally unavailable for resumable OTA because an image
without a verified SHA-256, architecture, and version cannot be safely matched
to a staged slot or committed.

The Web UI reads and hashes the image incrementally. It scans the whole file
for the identity (the linker may place it at any offset), validates the ESP32-S3
header and appended image checksum, and then resumes only a matching remote
session. Selecting a different file never discards the existing session.

Build and upload in one command:

```sh
./scripts/bo-idf --arch n16r8 --host 192.168.1.50 --password "my-device-password"
```

For unattended updates, add `--force`. It skips the commit prompt and waits
for the rebooted firmware to report that the OTA image is confirmed; it does
not need a Web UI reload:

```sh
./scripts/bo-idf --arch n16r8 --host 192.168.1.50 --password "my-device-password" --force
```

Upload an image that was already built:

```sh
./scripts/ota-idf --arch n16r8 --host 192.168.1.50 --password "my-device-password"
```

Upload a firmware image stored elsewhere (for example, a release downloaded
outside this repository):

```sh
./scripts/ota-idf --arch n16r8 --host 192.168.1.50 --password "my-device-password" \
  --image ~/Downloads/shotstopper.bin
```

For a controller using its SoftAP, replace the host with `192.168.4.1`:

```sh
./scripts/bo-idf -a n16r8 -H 192.168.4.1 -t "my-device-password"
```

Replace `n16r8` with `n8r4` if that is your board. `bo-idf` builds first;
`ota-idf` uses `build-idf/<architecture>/shotstopper.bin` and therefore
requires that you have run `build-idf` already. Short aliases `o-idf` and
`bo-idf` are also available.

Build and flash flow: [Build environment](../BUILD.md). Script flags and
CLI reference: [Build scripts](../SCRIPTS.md).

Related: [Wi-Fi](../settings/wifi.md), [AP](../settings/ap.md),
[Emergency recovery](../EMERGENCY_RECOVERY.md).

## Session-start troubleshooting

1. Read the reported HTTP status, `error`, and `message`; do not retry blindly.
2. For `PENDING_VERIFY`, inspect `confirmBlockReason`, `confirmAttempts`,
   `confirmLastError`, and `bootState` in OTA status. `WAIT_UPTIME`, `WAIT_HTTP`,
   `WAIT_BLE`, `WAIT_CYCLE`, and `WAIT_RELAY` identify the unmet gate.
   `FLASH_BUSY`, `STATE_ERROR`, `CONFIRM_ERROR`, and `ROLLBACK_ERROR` preserve
   the actual error and retry at one-second intervals. If it persists beyond
   180 s at idle, save the diagnostic export rather than repeatedly uploading.
3. For `CONFIG_LOCKED_DURING_ACTIVE_CYCLE`, stop the cycle and wait for Ready.
4. For a matching partial image, select the same file or run the same CLI
   command; it resumes automatically from `nextOffset`.
5. For a different partial image, preserve it unless it is intentionally being
   replaced. Use **Discard** in Admin or `--discard-ota-session` explicitly.
6. For `NO_IDENTITY` or a controller without the resumable session schema,
   install one current image over USB. Do not use `--no-check` or a legacy OTA
   fallback to bypass the incompatibility.

## Protocol details and verification

Session POST accepts exactly `size`, `sha256`, `arch`, `version`, `transferId`.
PATCH offsets and non-final lengths must be multiples of 4096; the advertised
maximum remains 65536. On interruption, query the session and reconstruct the
next range from `nextOffset`, including backward movement after reboot.
`running.imageSha256` and `staged.imageSha256` are the verified appended image
digest; session `sha256` remains the hash of the entire file, including that
digest. `bootState` uses IDF image states (unknown read failure is -2).

Run the focused functional regression suite with `bash src/tests/run_ota_tests.sh`.
Hardware qualification must cover Web and CLI without a browser open during
confirmation, fragmented/disconnected transfers, power loss around a journal
checkpoint, equal-version builds, same-ROM reinstall, and rollback. Host
tests do not certify radio timing, physical circuit safety, or power-cut behavior.
