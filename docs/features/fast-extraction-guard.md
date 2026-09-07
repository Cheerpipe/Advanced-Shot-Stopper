# Fast extraction guard

Optional brew-by-weight enhancement, **on by default**. It covers shots that
reach the target **too quickly** — often channeling or a grind that is too
coarse — where you may prefer a longer extraction. It cannot guarantee a better-tasting
result; disable it if you want the normal weight endpoint.

You still set a target weight. The guard adds a min BBW brew time and a
maximum recovery weight.

## When it applies

Automatic brew-by-weight with a usable scale. It does not run when BBW is
off, on rinses, or on manual no-scale shots.

Elapsed time is measured from cycle start (machine circuit close). The learned stop
offset applies to both the target and the max recovery weight.

Fast extended and Slow extended are mutually exclusive. If Fast already
extended the shot, Slow does not take over.

## Parameters

Active preset, **Settings → Brew**. Defaults below are factory Double;
[Single has different recovery weights](presets.md#factory-recipes). The ON/OFF switch is also on
**Home → Quick Settings** (read-only when brew by weight is off).

| Setting | Default | Range / notes | Effect on the shot |
| --- | --- | --- | --- |
| **Enable** | ON | ON / OFF | Master switch for the extended-shot recovery. |
| **Min BBW brew time (s)** | 28 s | 5–55 s; less than Slow's decision time when both are on | Earliest normal BBW cut. |
| **Max recovery weight (g)** | 42.5 g | 10–200 g; above target | Ceiling if the shot continues past target; learned offset applies. |

When Fast is also on with Slow, factory Double uses a normal BBW window
between **28 s** and **44 s**.

## How it works

1. **Normal stop** — the scale reaches the target at or after the minimum brew
   time → machine circuit opens at the target (`normal_target`).
2. **Too fast** — the target arrives *before* the min BBW brew time → the
   shot enters **extended** mode until either:
   - **Max recovery weight** (`extended_max_weight`), or
   - **Min BBW brew time** is reached *and* the scale is still at least at
     target (`extended_min_time`).

## Example

With a 36 g target, 1.5 g learned offset, 28 s minimum, and 42.5 g recovery
ceiling, the normal threshold is about 34.5 g:

- Threshold reached at 30 s: normal target stop is eligible.
- Threshold reached at 22 s: Fast extends until the recovery threshold
  (about 41 g) or 28 s with target still satisfied, whichever occurs first.
- Scale lost during extension: weight decisions pause; A→M and other
  applicable limits remain responsible until the scale recovers.

Prediction, sample confirmation, and dripping affect the final cup weight.

With a local buzzer compiled in, **Alerts → Extended shot pulse** can mark
the extension. See [Alerts](../alerts.md).

Related: [Brew by weight](brew-by-weight.md),
[Slow extraction guard](slow-extraction-guard.md).
