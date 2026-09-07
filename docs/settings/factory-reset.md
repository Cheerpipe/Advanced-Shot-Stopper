# Factory reset

Erases saved configuration and returns the controller to first-boot
credentials. Firmware on the flash is not erased.

## What is erased

Wi-Fi (STA and last-known-good network), workflow settings, presets,
calibration (including learned offset and A→M samples), preferred scale,
BLE Companion preference, shot history, and last shot. The device
password returns to **`ineedacoffee`**. The device then restarts.

## Ways to run it

Use Web/USB reset while the machine is idle, the activator is OFF, the relay
is open and the controller is Ready. The physical recovery gesture runs before
Wi-Fi and keeps the relay open. Check actual idle state on momentary machines.

| Path | How | Notes |
| --- | --- | --- |
| **Web UI** | Admin → unlock with the device password → Factory reset, confirm `ERASE_ALL_SETTINGS` | Device password required. Device restarts. |
| **USB serial** | `FACTORY_RESET` | Same erase. See [USB serial CLI](../SERIAL_CLI.md). |
| **Paddle gesture** | Power on with paddle ON, then five `OFF→ON` cycles | Last-resort. See [Emergency recovery](../EMERGENCY_RECOVERY.md). |

A shorter paddle gesture (three cycles) restores the device password / forgets STA
**without** erasing recipes, calibration, or history.

## After reset

Follow [first setup](../GETTING_STARTED.md) using the
[factory AP connection](ap.md#first-connection). Scale preference returns to **Preferred only**, with
**First detected** shown until the first compatible scale connects
successfully. See [Scales](scales.md), [AP](ap.md), and the
[README first connection](../../README.md#first-connection).

## If reset fails

Before changing settings, factory reset writes a durable recovery intent. If
that write fails specifically because NVS is full, it removes only shot-history
and last-shot blobs, retries the intent once, and then performs the full reset.
Timeouts, corruption, and other storage errors do not trigger this space
recovery. If the intent still cannot be saved, settings remain unchanged and
the controller does not restart. Check **Diagnostic → NVS** for capacity and
the last storage error.

Related: [Wi-Fi](wifi.md), [Emergency recovery](../EMERGENCY_RECOVERY.md),
[FAQ](../FAQ.md).
