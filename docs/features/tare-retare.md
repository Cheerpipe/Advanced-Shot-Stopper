# Tare and retare

Two automatic steps establish the scale's zero for brew-by-weight: an initial
tare when the shot starts, and a second tare if you put the cup down after
the paddle is already ON.

Both are **on by default**. You do not need to press tare on the scale.

## Automatic tare at start

When an automatic shot begins with a usable scale, the firmware sends tare
so the empty pan (or cup already on the scale) reads ~0 g. After that,
**Post-tare grace** waits for the reading to settle before weight is used
to stop or control the shot. First-drop detection still runs against that
tare zero.

## Automatic late-cup retare

If the cup is not on the scale yet, you can place it after the shot has
started. Cup-presence detection watches for a stable load at or above
**Minimum cup weight** (`minimumCupWeightG`, default **10 g**; cardboard
cups around 12–20 g use this same setting). When a cup is **placed** inside
the **retare window** (default **4 s**), the firmware tares again
once—automatically—without restarting the shot timer.

Putting the cup down is not first drop and does not block retare, whether
the cup is a light cardboard cup just above the configured minimum or a
heavier ceramic cup. A finger tap is also not first drop. After the late
tare, post-tare grace runs again so the empty-cup weight cannot cut the
shot; [Cup protection](cup-protection.md) still blocks weight stop for the
full BBW protection window.

## When it applies

Initial tare can also run on a timer-only shot with a usable scale. Late retare
requires automatic BBW; timer-only (BBW off) and manual no-scale cycles skip it
and BBW protection. Post-tare grace is inactive when **Automatic tare** is off.

## Parameters

Configured under **Settings → Machine and scale → Tare** (and **Cup** for
presence thresholds). Full tables: [Tare](../settings/tare.md),
[Cup](../settings/cup.md).

## Example

Cup already on the scale: shot starts → one automatic tare → brew continues.

Cup placed two seconds after paddle ON: start tare runs, then cup detection
fires a second automatic tare inside the 4 s window. The shot timer does
not restart; weight stop stays blocked until BBW protection ends.

If you place a cup after the 4 s default window, automatic retare is no longer
available for that placement. If **Automatic tare** is off, prepare the scale's
zero yourself; do not assume post-tare settling protection is active.
[Require cup to start](cup-protection.md) is an alternative when you always
want the cup in place before brewing.

Related: [Cup protection](cup-protection.md), [Brew by weight](brew-by-weight.md),
[Tare](../settings/tare.md), [Cup](../settings/cup.md).
