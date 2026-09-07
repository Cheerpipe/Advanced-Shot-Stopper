# Slow extraction guard

Optional brew-by-weight enhancement, **on by default**. It is the inverse of
[Fast extraction guard](fast-extraction-guard.md): shots that have **not**
reached the target by a maximum brew time — often a grind that is too fine —
should not wait all the way to the machine circuit wall.

## When it applies

Automatic brew-by-weight with a usable scale. It does not run when BBW is
off, on rinses, or on manual no-scale shots.

BBW still wins by weight: if the target arrives on time, the cut is a normal
target stop. Fast extended and Slow extended cannot both own the same shot.

Elapsed time is measured from cycle start (machine circuit close). The learned stop
offset applies to the min recovery threshold (`min recovery − offset`).

Max BBW brew time is a **decision point**, not a replacement for Max BBW time or
the hard 60 s cap.

## Parameters

Active preset, **Settings → Brew**. The ON/OFF switch is also on
**Home → Quick Settings** (read-only when brew by weight is off).

| Setting | Default | Range / notes | Effect on the shot |
| --- | --- | --- | --- |
| **Enable** | ON | ON / OFF | Master switch for the slow-shot recovery. |
| **Max BBW brew time (s)** | 44 s | 5–55 s; greater than Fast's minimum when both are on | Decision time for the slow-shot branch. |
| **Min recovery weight (g)** | 34 g Double / 16 g Single | 10–200 g; below target | Recovery floor; learned offset applies. |

## How it works

1. **Normal stop** — the scale reaches the target at or before max BBW brew time
   → machine circuit opens at the target (`normal_target`). Slow does not fire.
2. **Too slow** — max BBW brew time is reached *without* the target:
   - Already at or above **min recovery** → cut now (`slow_max_time`).
   - Still below that floor → **extended** until min recovery
     (`slow_min_weight`) or until the machine circuit / Max BBW time wall.

## Examples

Factory Double: 36 g target, 1.5 g offset, Slow decision at 44 s, recovery
weight 34 g (effective threshold about 32.5 g).

- Normal target threshold reached before 44 s: normal stop, no Slow extension.
- At 44 s, weight is 33 g: the recovery threshold is satisfied, so Slow can stop.
- At 44 s, weight is 20 g: continue toward the recovery threshold. The default
  50 s Max BBW time can stop the shot first; Slow does not grant extra time
  beyond that limit.
- Fast already extended this shot: Slow does not take over.

[Single](presets.md#factory-recipes) uses different weights. Final cup weight
includes dripping; these thresholds describe the decision, not a guaranteed yield.

**Alerts → Slow extended pulse** can mark the extension when a local buzzer
is compiled in. See [Alerts](../alerts.md).

Related: [Brew by weight](brew-by-weight.md),
[Fast extraction guard](fast-extraction-guard.md).
