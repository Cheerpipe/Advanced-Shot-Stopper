# Scales

Which Bluetooth scale to use, how long to wait after a shot for drip, and
Bookoo-specific volume / combined tare. Machine-level, under
**Settings → Machine and scale → Scales**.

Daily defaults assume a **Bookoo** Themis Mini or Ultra. **Acaia**,
**Felicita**, **AtomHeart Eclair**, **Decent**, **DiFluid Microbalance**,
**MyScale**, **Varia AKU**, **Eureka Precisa** (named GAP), and **WeighMyBru**
are also supported through the vendored EspressoScaleBLE library. Compatibility
details:
[Scale compatibility](../../libraries/EspressoScaleBLE/README.md#scale-compatibility).

Timemore Black Mirror DUO, Timemore Dot, Acaia Umbra, and Eureka units that
advertise no GAP name are not supported.

## When it applies

A usable scale is required for automatic brew-by-weight. If the scale is
missing, see [No-scale BBW](no-scale-bbw.md). If it drops mid-shot, see
[A→M time guard](../features/auto-to-manual.md).

**Drip delay** runs after machine circuit opens. It is used for Last Shot, history,
offset learning, and eligible A→M samples.

## Parameters

| Setting | Default | Range / notes | Effect |
| --- | --- | --- | --- |
| **Scale preference** | Preferred only | First available / Prefer selected / Preferred only | **First available** connects whichever compatible scale appears first and never locks it. **Prefer selected** waits briefly for the preferred scale, then accepts another. **Preferred only** connects only to the saved preferred scale. If either selected mode has no saved scale yet, it uses the bootstrap flow below. |
| **Preferred scale** | First detected | First detected, No preferred, or a BLE-seen scale | **First detected** is shown only while no preferred MAC is saved and **Prefer selected** or **Preferred only** is active. The controller scans by compatible name and adopts the first scale that completes a successful connection; advertisements and failed connections are not enough. **First available** instead shows **No preferred** because that mode never locks a scale. **Clear preferred** pauses discovery for 30 s, keeps history, and keeps the current Scale preference. |
| **Drip delay (s)** | 3.0 s | 0–10 s | Wait after a shot ends before capturing the final post-drip weight. `0` finalizes on the next control loop with no intentional window. |
| **Timer stop extra delay (ms)** | 0 ms | 0–1000 ms | Pad after the scale timer catches up to circuit whole seconds, before `STOP_TIMER`. `0` stops in that same instant. Does not delay the local machine circuit beep. |
| **Bookoo combined command** | ON | ON / OFF | Combined tare + start-timer. Requires automatic tare at shot start. Also listed under [Tare](tare.md). |
| **Mute scale in Buzzer only** | ON | ON / OFF | Bookoo/generic: send silence (volume 0) after the first valid weight on the first connection of that scale in this Shot Stopper session. Reconnecting does not resend it. Applies only in **Buzzer only**. |
| **Scale volume** | 4 | 1–5 or Disabled | Bookoo/generic: set after the first valid weight on the first connection of that scale in this Shot Stopper session. Reconnecting does not resend it. Explicit setting changes still apply to a stable link. Applies only in **Scale only** and **Scale priority**. |
| **AtomHeart Eclair** | informational | — | Uses normal tare/timer commands. No configurable volume, beep, mode, combined command, or documented command sound. In Buzzer only and Scale priority, alerts use the local buzzer; Scale only omits unsupported sounds. |

If the scale disconnects or **notifications go silent** during an automatic
extraction, weight control is suspended. Rejected brew samples (post-tare,
slew) and a stable accepted weight do not count as a lost scale. Recovery
needs three coherent samples on the current connection. Physical stop behavior depends on the selected switch/mode; applicable time
limits remain in force.

## Example

The integrated scale protocols expose weight/timer readings and firmware
command results, but no verified physical-button tare notification. Preserving
a cup at zero after such a button press is therefore not supported by the
current integration; weight alone cannot distinguish it from cup removal.
Firmware idle tare preserves the known cup reference. See [Tare](tare.md).

On a new controller, **Preferred only** and **First detected** are selected.
Turn on your Bookoo: after its first successful connection, its MAC and name
replace **First detected** and are saved. From then on, other scales may be
remembered in history but are not connected. After each shot, the firmware
waits 3 s of drip before storing the final weight used for offset learning.
Bookoo volume is not rewritten on reconnections during the same controller
session. Restarting the Shot Stopper or explicitly clearing that preferred
scale opens a new first-connection session for it.

If no compatible scale is available, the controller stays in the bootstrap
name scan indefinitely and does not silently change the saved preference.
Choosing **Prefer selected** before a scale has been adopted uses the same
bootstrap; after adoption, its normal preferred-first fallback applies.
Changing the preference mode or selected scale restarts an in-progress search
immediately with the new filter. If **Preferred only** is enabled while a
different scale is connected, that connection is closed before directed
discovery begins.

## Replace a scale or diagnose a missing connection

1. Turn off other compatible scales and close phone apps connected to the one
   you want.
2. Select the intended remembered scale, or **Clear preferred** for a new one.
   Clearing pauses discovery for 30 s; it does not erase scale history.
3. With **Preferred only** and no saved preference, let the intended scale
   complete its first connection. Check Home for fresh weight, not just a name.
4. If discovery is slow, check **Admin → Bluetooth → Scan intensity**:
   factory **Aggressive**, with Normal and Light reducing scan duty.
   Optional [Power management](power-management.md) temporarily uses Light in
   idle; switching it off restores the saved intensity.
5. **BLE Companion** is separate from scale support. It is off by default;
   changing its preference takes effect after reboot and does not disable the
   scale connection. The Admin/USB status distinguishes saved and active state.

Related: [Brew by weight](../features/brew-by-weight.md), [Tare](tare.md),
[Alerts](../alerts.md).
