# Slow extraction guard

Optional brew-by-weight enhancement, **on by default** on the Adaptive presets.
It is the opposite of the [Fast extraction guard](fast-extraction-guard.md):
a shot that has **not** reached the target by the end of your shot time window
— often a grind that is too fine — stops a little early instead of running on
to the max shot time.

## When it applies

Automatic brew-by-weight with a usable scale. It does not run when BBW is
off, on rinses, or on manual no-scale shots.

Time is measured from the start of the shot, pre-infusion included. The learned
drip offset applies to every stop. The guard never replaces Max BBW time or the
hard 60 s limit: those always stop the shot.

## Parameters

Active preset, **Settings → Brew**. The ON/OFF switch is also on
**Home → Quick Settings** (read-only when brew by weight is off).

| Setting | Default | Range / notes | Effect on the shot |
| --- | --- | --- | --- |
| **Enable** | ON | ON / OFF | Lets slow shots stop sooner. |
| **Max BBW brew time (s)** | 34 s Double / 32 s Single | At least 1 s after Fast's time and 1 s before Max BBW time, in 0.5 s steps | End of the shot time window: a shot reaching the target after it is slow. |
| **Min recovery weight (g)** | 33 g Double / 16.5 g Single | Target − 0.5 g down to target − 10 g, never below 5 g | The least a very slow shot may yield. |

## How it works

With Slow on, the cut line stays on the target until the maximum brew time and
then, over the next 5 seconds, eases smoothly down to the minimum recovery
weight, where it stays.

1. **On time** — the shot reaches the target by the maximum brew time: it stops
   at the target (`normal_target`).
2. **Slow** — the shot is still short of the target at that time: it stops when
   it meets the lowered cut line (`slow_min_weight`). A shot that is only a
   little late loses only a little yield; a very slow one stops at the minimum
   recovery weight, or at Max BBW time if it never gets there.

## Examples

Factory Double · Adaptive: 36 g target, window until 34 s, 33 g minimum.

- 36 g reached at 32 s: normal stop at 36 g.
- A shot that would reach 36 g at 36 s stops a little earlier, near 35 g.
- A very slow shot stops once it reaches 33 g, or at the 50 s Max BBW time.

[Single](presets.md#factory-recipes) uses different values. Final cup weight
includes dripping; these weights describe where the cut line stands, not a
guaranteed yield.

**Alerts → Slow extended pulse** marks a shot whose cut line dropped below the
target when a local buzzer is compiled in. See [Alerts](../alerts.md).

Related: [Brew by weight](brew-by-weight.md),
[Fast extraction guard](fast-extraction-guard.md).
