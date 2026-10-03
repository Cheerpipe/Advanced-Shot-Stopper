# Firmware mode

Master switch that turns every Shot Stopper brewing feature on or off while
leaving the controller in place.

## When you would use it

- You want the machine to behave exactly as it did before the controller was
  installed, but you are not ready to disconnect the hardware.
- You are troubleshooting and want to rule the controller in or out quickly.
- You are lending the machine to someone who only wants the plain switch.

## What happens when you turn it off

Saving the switch restarts the controller once, safely: a running pour always
finishes first. After the restart the controller becomes transparent:

- The relay follows the physical paddle or switch one-to-one, the same way the
  machine's own wiring would. Nothing else touches the machine.
- Brew by weight, guards, presets, rinse, automatic tare, alerts, webhooks,
  the scale Bluetooth connection, time sync, and the Linea Micra cloud
  connection all stop working.
- The web interface shows only the **Admin** page. Every other tab is hidden
  and any address other than Admin's takes you there; the **Diagnostic**
  address is the one exception, because support may need its measurements.
  Inside Admin you keep Network, Power
  management, Frontend, Device password, Restart, Firmware update, and
  Factory reset. The controls of disabled features are hidden with them:
  the Bluetooth section and its scan-mode options inside Power management,
  Date & time (clock sync and time zone), and Webhooks.
- Recovery mode, firmware updates over Wi-Fi (OTA), and the USB serial CLI
  keep working, so you can always reach the controller.

There is no time limit in compatibility mode: the relay keeps following the
paddle or switch even when it is held for more than 60 seconds. Releasing it
opens the relay. Turning the firmware back on restores its normal time limits.
Automatic [Micra backflush](linea-micra.md#automatic-backflush) is available
only with the firmware enabled and WebSocket monitoring connected.

## Nothing is lost

The switch does not erase anything. Presets, guards, scale preferences,
machine integration, alerts, and every other setting stay saved exactly as
they were. When you turn the switch back on and the controller restarts, the
firmware works the way it did before you turned it off.

A [factory reset](factory-reset.md) also turns the firmware back on.

## Where to find it

**Admin** (unlock with the device password) **→ Firmware → Enable Open Brew
by Weight**. The change takes effect after **Save firmware settings** and the
restart that follows. Unplug-level safety behavior does not change: the relay
still opens on power loss, and a hardware safety fault still stops the
machine in both modes.

| Setting | Default | Notes |
| --- | --- | --- |
| **Enable Open Brew by Weight** | On | Off = transparent compatibility mode. Saved settings are kept. |
