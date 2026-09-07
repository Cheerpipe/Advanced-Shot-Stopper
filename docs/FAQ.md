# Troubleshooting

Start with the symptom below. Defaults refer to fresh factory settings; a
saved recipe or migrated device may differ. [First setup](GETTING_STARTED.md)
covers the normal path.

## Shot and scale

### The shot reached target but did not stop

Check, in order:

1. BBW is on in **Home Quick Settings**, not just in the saved recipe.
2. Home shows fresh usable scale readings.
3. Initial [BBW protection](features/cup-protection.md) has ended.
4. [Fast guard](features/fast-extraction-guard.md) is not deliberately extending.
5. [Original paddle mode](settings/paddle.md#original) is not waiting for OFF.
6. On momentary firmware, the machine state permits an automatic stop pulse.

Use the stop action for your physical switch/mode if the result is unexpected.
[Auto paddle mode](settings/paddle.md#auto) does not interpret OFF as early stop.

### The shot ended above or below target

Read Last Shot's stop detail before changing calibration.

| Result | Likely check |
| --- | --- |
| Above target, fast shot | Fast guard may extend toward its recovery weight. |
| Below target, slow shot | Slow guard may choose its recovery floor. |
| Around 50 s on factory Double | Max BBW time; distinct from Slow's 44 s decision. |
| After scale loss | A→M deadline is measured from shot start, not disconnect. |
| Ordinary stop followed by extra dripping | Learned offset and drip delay; see [BBW](features/brew-by-weight.md). |

Prediction, sample timing and dripping mean the weight at stop need not equal
the final stored weight. Guard behavior does not guarantee extraction quality.

### The scale disconnected

Weight stop pauses while reconnection continues. Three coherent readings on
the recovered link can restore it. [A→M](features/auto-to-manual.md) and other
applicable limits still act. A steady unchanged weight is not a lost stream.

If the scale was absent at start, [No-scale BBW](settings/no-scale-bbw.md)
controls whether the attempt is blocked or manual. A scale arriving later
does not retroactively make every manual cycle an automatic one.

### Can I place the cup late or brew without weight control?

A cup placed within the [retare window](features/tare-retare.md) can be tared
automatically. Later placement is not covered. **Require cup to start** blocks
the start instead.

For a timer-only session, turn BBW off in Home. Tare/timer remain available
when supported; weight stop, late retare and Max BBW time do not. Home's
session selection does not overwrite the saved preset.

### Does the 60-second limit always stop water?

It limits electrical relay closure. On paddle installations that normally stops
brewing. A momentary machine needs a stop pulse and valid state conditions;
without scale/reed evidence, a tap-started machine may not receive it. Read
[Momentary limits](settings/momentary.md#stopping-and-time-limits).

## Network and access

### I cannot find the controller's Wi-Fi

On fresh settings, the [AP](settings/ap.md) starts at boot. With saved Wi-Fi,
fallback begins after roughly 25 s without association. Automatic AP shuts
down after 3 minutes with no associated clients and does not restart after a
later successful home-network connection drops. Use USB `AP_START` or reboot.

### I saved Wi-Fi and the old page never returned

Reconnect your phone/computer to the home network, find the controller's DHCP
address in the router or USB `NET_STATUS`, then open that address and claim
the UI within 3 minutes. The AP address cannot follow a change to a different
network. Unconfirmed changes revert; see [Wi-Fi](settings/wifi.md).

### Controls are locked or another browser took over

Reload claims the Web UI. Admin unlock is a separate password check. Idle
timeouts, another browser's claim, and an active shot can each restrict
controls for different reasons. See [Web access](GETTING_STARTED.md#web-access).

### I forgot the password or cannot reach any interface

Use [USB commands](SERIAL_CLI.md) or
[physical recovery](EMERGENCY_RECOVERY.md). Resetting only the password over USB
preserves Wi-Fi; access recovery also forgets the network. Factory reset erases
much more. Compare the procedures before choosing one.

### The scale connects slowly or the UI stutters

Close other scale-connected apps. Check the saved preferred scale and
**Admin → Bluetooth → Scan intensity**. Factory default is **Aggressive**;
Normal/Light use less scanning radio time. Try lower intensity if idle scanning
hurts UI response. **Admin → Wi-Fi → Wi-Fi sleep** can also affect latency.
See [Scales](settings/scales.md) and [Wi-Fi](settings/wifi.md).

### Why is there no remote Start or rinse?

They are disabled in default firmware. An explicit development build can
enable them, but Admin unlock alone cannot. Remote Stop remains privileged.
See [build options](BUILD.md#5-build).

## Hardware and compatibility

- **Boards / wiring:** [Hardware](HARDWARE.md). A GPIO map is not a complete
  machine installation guide.
- **Scale models / missing timer or sound:** [compatibility table](../libraries/EspressoScaleBLE/README.md#scale-compatibility).
  Implemented and physically tested support are different.
- **No USB port:** default app CDC needs GPIO4 held to GND at reset.
  ROM download uses BOOT + RST; see [USB console](HARDWARE.md#usb-console-jumper).
- **LED or beeps:** the connection LED is not a brew or safety-ready indicator.
  Output depends on [Alerts](alerts.md), local buzzer and scale capabilities.
- **Change GPIOs:** rebuild with the reviewed source/compile-time map; the Web
  UI does not configure safety-critical pins.

## Safety and diagnostics

After a watchdog/panic reset, the relay is forced open and the interrupted shot
does not resume. Persistent hardware/feedback faults may still block starting.
An open K1 cannot stop a welded contact; see [isolation](HARDWARE.md#isolation-must).

To report an issue, include firmware version, board and machine type, scale
model/firmware, exact gesture/settings, and redacted diagnostic export. Never
include passwords or webhook secrets. [USB HEALTH](SERIAL_CLI.md#diagnostics)
and [shot history](features/shot-history.md) help explain the observed result.

## Where to change it

Use the [settings index](README.md#settings) for parameter references,
[OTA troubleshooting](features/ota.md#session-start-troubleshooting) for update
errors, and [Home Assistant](features/home-assistant-webhooks.md#if-nothing-arrives)
for delivery problems.
