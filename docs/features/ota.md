# Firmware installation

Firmware installation is USB-only. Wi-Fi OTA is disabled because every
persistent store now starts at schema 1 and older data is intentionally
discarded.

Connect a USB data cable, build the exact hardware and machine profile, and
flash the complete project image:

```sh
./scripts/dev build flash --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --port /dev/cu.usbmodem2101 \
  --erase-all
```

`--erase-all` is required when replacing an installation with this schema-1
baseline. It erases firmware slots, settings, Wi-Fi credentials, presets,
scale preferences, shot history, curves, and the last-shot record. Export or
record anything you need before flashing.

If a network client requests an OTA endpoint, the controller reports that
firmware installation requires USB. The developer command facade rejects OTA
pipelines before contacting the controller.

For recovery after a failed boot, use the USB procedure in
[Emergency recovery](../EMERGENCY_RECOVERY.md).
