# OTA image identity contract

The firmware, browser, and developer scripts use this contract to decide
whether an ESP32-S3 application image belongs to Open Brew by Weight and targets the
same architecture, assembled hardware, and coffee machine as the running
controller.

For update steps, use [OTA](ota.md). This page is for firmware/browser/CLI
implementers; these checks do not replace the controller's verification.

## Container checks

- ESP image magic: `0xE9` at byte 0.
- ESP chip ID: `0x0009` (ESP32-S3), little-endian, at bytes 12–13.
- `esp_app_desc_t` magic: `0xABCD5432`, little-endian, at byte 32.
- Project name at byte 80, capacity 32 bytes: `openbrewbyweight` for native IDF or
  `arduino-lib-builder` for the Arduino core build.
- `hash_appended` at byte 23 must be 1. The last 32 bytes are the SHA-256 of
  every preceding byte in the application image.

## Open Brew by Weight tag

The image contains one generated ASCII tag:

```text
OPENBREWBYWEIGHT_FW_TAG_V2|arch=<arch>|hw=<hardware>|machine=<machine>|ver=<version>|packed=<uint32>|END
```

- The tag may occur at any byte offset. Its position is linker-dependent and
  is never a compatibility condition.
- `arch` contains 1–15 lowercase ASCII letters or digits and cannot be
  `unknown`.
- `hw` and `machine` contain their profile IDs plus compatibility revisions,
  or `legacy` for builds made without named profiles. Each value contains
  1–63 lowercase ASCII letters, digits, or hyphens.
- `ver` contains 1–47 ASCII letters, digits, `.`, `+`, `-`, or `_`.
- `packed` is an unsigned decimal 32-bit integer with at most 10 digits.
- The parser body, including `|END`, is limited to 256 bytes.
- Unknown fields are ignored. A malformed candidate does not hide a later
  valid candidate.
- Parsers must handle the prefix, fields, and terminator across arbitrary
  stream boundaries using bounded memory.

## Session identity

The immutable image identity is:

```text
size + SHA-256(full .bin) + arch + hardware + machine + version
```

`transferId` identifies an upload session. A client reconnecting with the same
image adopts the server's existing `transferId` and continues from the
validated `nextOffset`. A different image must explicitly discard the old
session before it can create a new one.

The session JSON contains only those six identity fields and `transferId`.
Parser metadata (`packed`, `tagOffset`, `projectName`, `imageSha256`) is local
and must not be copied into this strict request body.

For post-boot verification, `imageSha256` is the appended digest of the image
content, excluding its final 32 bytes. It is different from session `sha256`,
which covers the entire `.bin`. Match it to `running.imageSha256`, require a
new `bootId`, and then require `confirmed` before declaring OTA success.

The controller remains authoritative: local validation is a fast preflight,
while the controller repeats the header, tag, architecture, hardware profile,
machine profile, version, complete SHA-256, and ESP image verification before
staging or changing the boot slot.
