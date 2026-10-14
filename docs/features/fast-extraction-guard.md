# Fast extraction guard

Optional brew-by-weight enhancement, **on by default** on the Adaptive presets.
It covers shots that reach the target **too quickly** — often a grind that is
too coarse — and lets them run a little longer for more yield. It cannot
guarantee a better-tasting result; turn it off (or use a Fixed preset) to
always stop at the target.

You still set a target weight. The guard adds a time — the start of your shot
time window — and a maximum weight a fast shot may reach.

## When it applies

Automatic brew-by-weight with a usable scale. It does not run when BBW is
off, on rinses, or on manual no-scale shots.

Time is measured from the start of the shot, pre-infusion included. The
learned drip offset applies to every stop, so the cup lands where the cut line
says.

## Parameters

Active preset, **Settings → Brew**. Defaults below are factory Double ·
Adaptive; [the other presets differ](presets.md#factory-recipes). The ON/OFF
switch is also on **Home → Quick Settings** (read-only when brew by weight is
off).

| Setting | Default | Range / notes | Effect on the shot |
| --- | --- | --- | --- |
| **Enable** | ON | ON / OFF | Lets fast shots run longer. |
| **Min BBW brew time (s)** | 27 s | At least 2 s after BBW protection, at least 1 s before Slow's time, in 0.5 s steps | Start of the shot time window: a shot reaching the target before it is fast. |
| **Max recovery weight (g)** | 40 g | Target + 0.5 g up to target + 10 g, and at most a quarter more than the target | The most a very fast shot may yield. |

## How it works

The controller stops a shot where its **cut line** meets the weight. With Fast
on, the cut line starts at the maximum recovery weight and, over the 5 seconds
before the minimum brew time, eases smoothly down to the target. From that time
on it is the target.

1. **On time** — the shot reaches the target after the minimum brew time:
   it stops at the target (`normal_target`).
2. **Fast** — the shot runs ahead of the window: it keeps going until it meets
   the cut line, which is above the target (`extended_max_weight`). A very fast
   shot meets it at the maximum recovery weight; a shot that is only a little
   early gets only a little more yield. The change is gradual, so reaching the
   target a fraction of a second earlier never changes the cup by more than a
   fraction of a gram.

## Example

Factory Double · Adaptive: 36 g target, window from 27 s, 40 g maximum.

- A shot reaching 36 g at 30 s stops at 36 g.
- A shot that would reach 36 g at 24 s keeps running and stops near 38 g.
- A gusher reaching 36 g at 16 s stops at 40 g.
- Scale lost during the shot: weight decisions pause; the A→M time guard and
  the other time limits remain responsible until the scale recovers.

Prediction, sample confirmation and dripping affect the final cup weight.

With a local buzzer compiled in, **Alerts → Extended shot pulse** marks a shot
whose cut line rose above the target. See [Alerts](../alerts.md).

Related: [Brew by weight](brew-by-weight.md),
[Slow extraction guard](slow-extraction-guard.md).
