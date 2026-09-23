# USB image identity

The firmware image identity is checked before USB installation. The selected
architecture, hardware profile, and machine profile must match the image tag
and the connected controller's partition layout.

Use the public facade with the exact profiles:

```sh
./scripts/dev build flash --confirm \
  --hardware esp32-s3-relay-x1-speaker \
  --machine rancilio-silvia-pro-x \
  --port /dev/cu.usbmodem2101
```

For a schema-1 cutover or changed partition table, add `--erase-all` to erase
the complete flash before writing the bootloader, partition table, metadata,
and application. Wi-Fi OTA is disabled.
