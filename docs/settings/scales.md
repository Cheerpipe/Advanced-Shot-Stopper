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

## Scale power-off support

**Turn scale off when the machine powers off**
([La Marzocco Linea Micra](linea-micra.md#turn-scale-off-when-the-machine-powers-off))
switches the connected scale off over Bluetooth, but only when the scale
itself accepts a power-off command. The explicit support list:

| Scale | Power-off over Bluetooth |
| --- | --- |
| Bookoo Themis Ultra (firmware V4.0.0 and later) | Yes — BooKoo's published protocol, command `0x15`; ignored while charging |
| Bookoo Themis Ultra (firmware V3.1.2 and earlier) | No — the shutdown command is not in BooKoo's published contract for those versions; the scale ignores it |
| Bookoo Themis Mini | No — BooKoo's published protocol for the Mini has no shutdown command |
| Acaia (Lunar, Pearl S, Pyxis, Cinco, Proch) | No — no power-off command is documented for the Acaia protocol |
| Felicita (Arc) | No |
| AtomHeart Eclair | No |
| Decent Scale | No |
| DiFluid Microbalance / Ti | No |
| MyScale KP2048B | No |
| Varia AKU / Mini / Pro | No |
| Eureka Precisa | No |
| WeighMyBru | No |

Both Bookoo models advertise under the same `BOOKOO` name, so the controller
cannot tell a Themis Mini from a Themis Ultra; a shutdown sent to a Mini or
an older Ultra is simply ignored by the scale. With any unsupported scale
connected, enabling the option logs a warning and writes nothing to the
scale.

For Bookoo, the controller leaves at least 100 ms between Bluetooth commands.
Once it asks the scale to shut down, it sends no more commands on that
connection. After the disconnect it waits 500 ms before searching again, then
the first valid advertisement can reconnect normally; there is no additional
two-advertisement delay.

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
| **Mute scale in Buzzer only** | ON | ON / OFF | Bookoo/generic: send silence (volume 0) after the first valid weight on the first connection of that scale in this Open Brew by Weight session. Reconnecting does not resend it. Applies only in **Buzzer only**. |
| **Scale volume** | 4 | 1–5 or Disabled | Bookoo/generic: set after the first valid weight on the first connection of that scale in this Open Brew by Weight session. Reconnecting does not resend it. Explicit setting changes still apply to a stable link. Applies only in **Scale only** and **Scale priority**. |
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
session. Restarting the controller or explicitly clearing that preferred
scale opens a new first-connection session for it.

If no compatible scale is available, the controller stays in the bootstrap
name scan indefinitely and does not silently change the saved preference.
Choosing **Prefer selected** before a scale has been adopted uses the same
bootstrap; after adoption, its normal preferred-first fallback applies.
Changing the preference mode or selected scale restarts an in-progress search
immediately with the new filter. If **Preferred only** is enabled while a
different scale is connected, that connection is closed before directed
discovery begins.

If a visible scale repeatedly refuses a Bluetooth connection, the first few
attempts remain quick and later attempts spread out, up to five seconds apart.
The controller keeps trying automatically; fresh weight after a healthy
connection restores the quick initial timing. A scale that stops advertising
does not trigger blind connection attempts.

## Replace a scale or diagnose a missing connection

Home distinguishes **No sample** (connected but no accepted weight yet), **Stale**
(last weight too old), and **Disconnected**. A saved scale name or last displayed
weight does not prove the connection is usable. The first valid packet has a
5 s deadline; Bookoo disconnects after 8 s without valid packets. Automation
rejects readings older than 1 s, before that link timeout. Check
**Cup → Automatic tare** for the separate placement/readiness requirements.

1. Turn off other compatible scales and close phone apps connected to the one
   you want.
2. Select the intended remembered scale, or **Clear preferred** for a new one.
   Clearing pauses discovery for 30 s; it does not erase scale history.
3. With **Preferred only** and no saved preference, let the intended scale
   complete its first connection. Check Home for fresh weight, not just a name.
4. If discovery is slow, check **Admin → Power management → BLE scan mode**:
   factory **Balanced**, with **Aggressive** searching harder and **Relaxed**
   trading some speed for less radio time. Optional
   [Power management](power-management.md) temporarily uses Relaxed in
   idle; switching it off restores the saved mode. Below it, **Idle scan
   backoff** chooses how long to search at full strength with no scale in
   range before slowing down to save power — factory default **OFF** means
   this backoff itself never slows the search (the Power policy above can
   still use Relaxed while the machine sits idle), and with Aggressive or
   Balanced the first sign of a scale restores the saved mode on its own. **Scan boost on machine use**
   is the inverse safety net for people who leave the backoff on: switching
   on the machine with no scale connected searches at Aggressive for the
   chosen minutes (factory default **OFF**), overriding both slowdowns.

Related: [Brew by weight](../features/brew-by-weight.md), [Tare](tare.md),
[Alerts](../alerts.md).
