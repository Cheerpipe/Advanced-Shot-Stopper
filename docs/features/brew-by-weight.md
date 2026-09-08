# Brew by weight

Automatic brew-by-weight (BBW) stops the shot when the scale reaches the
recipe target, minus a learned drip offset. It is **on by default**.

Turn it off if you want the paddle (or switch) and the firmware 60 s cap
only. Tare and the shot timer still run; weight stop, cup protection at
start, automatic retare, Max BBW time, and offset learning do not.

## When it applies

BBW runs on an automatic shot that started with a usable scale. It does not
run on a rinse, a timer-only shot (BBW off), or a manual no-scale shot.

Once the selected paddle mode permits weight stopping and the start-of-shot
protection window ends (see
[Cup protection](cup-protection.md)), two fresh scale samples at or above
`target − learned offset` can request a stop, subject to Fast/Slow guards.
A short linear prediction can stop a moment earlier. The learned offset is
capped at 5.0 g and can be reset to a
baseline from the Web UI.

Automatic brew-by-weight cycles are limited by **Max BBW time** and a
firmware hard cap of **60 seconds**. Timer-only (BBW off) and manual
no-scale shots skip Max BBW time. In Original paddle mode, weight stop and the
operational wall are held off
while the paddle stays ON; the 60 s electrical cap remains. Momentary stop
pulses depend on machine-state confirmation: see
[Stopping and time limits](../settings/momentary.md#stopping-and-time-limits).

## Parameters

Defaults below describe factory Double; see [Presets](presets.md) for Single.
These live on the **active preset** under **Settings → Brew**, except where
noted. **Home → Quick Settings** can toggle brew by weight for the session
(Manual).

| Setting | Default | Range | Effect on the shot |
| --- | --- | --- | --- |
| **Brew by weight** | ON | ON / OFF | ON: stop by weight when a scale is usable. OFF: paddle, **Stop**, and the 60 s firmware cap only. Fast, Slow, A→M, Max BBW time, and No-scale BBW become read-only. |
| **Cutoff algorithm** | Linear prediction + adaptive EWMA | Linear regression + offset correction / Linear prediction + adaptive EWMA | Saved per preset; applies to the next shot. |
| **Target (g)** | 36 g | 10–200 g | Goal weight. Stop aims at `target − learned offset`. |
| **Max BBW time (s)** | 50 s | 5–60 s | Operational time limit for an **automatic BBW** cycle. Ignored on timer-only and no-scale shots. Cannot exceed the hard 60 s cap. |
| **Baseline offset (g)** | 1.5 g | 0–5 g | Seed used by **Reset learned stop offset to baseline**. Save this before reset. |
| **Learned stop offset** | starts at 1.5 g | 0–5 g | Subtracted from the target (and from Fast/Slow recovery weights). Updated from post-drip weight after good shots. |
| **Learning factor (α)** | 0.30 initially | Read-only | EWMA only; current gain, initial/learned provenance and collecting/evaluating status. |
| **Baseline learning factor (α)** | 0.30 | 0.01–1.00, step 0.01 | EWMA reset seed, saved per preset. Saving it preserves current offset, gain and evidence. |

Fixed behavior (not separate settings):

- Direct stop: two fresh samples at the threshold after BBW protection ends.
- Predictive stop: may request stop slightly before the threshold.
- Scale loss: weight control pauses; physical stop behavior and applicable time
  limits stay in force.
  See [A→M time guard](auto-to-manual.md).

## Example

Double recipe at 36 g, learned offset 1.5 g. After the protection window, the
firmware treats about **34.5 g** as the cut point so post-drip weight lands
near 36 g. This example assumes the Fast guard permits a normal stop and
no other guard has requested an earlier end. If target arrives too early,
Fast can deliberately extend the shot.

## Cutoff algorithms and learning

In **Settings → Brew → BBW**, choose **Cutoff algorithm**, then save the
preset. New controllers, new presets and factory recipe resets use **Linear
prediction + adaptive EWMA**. Upgrading settings without a selector selects
it once, retaining the old learned offset for regression and copying that offset
as the EWMA seed. A saved choice survives subsequent updates and reboot.

Both modes use ten eligible samples, a positive linear trend, the existing
minimum prediction horizon and two-sample direct confirmation. Invalid
prediction falls back to direct stopping and the existing time limits.
**Linear regression + offset correction** retains the original ordinary
least-squares calculation. Its stable API/CSV identifier remains `legacy`;
renaming the former Legacy label changes neither its calculation nor past data.
EWMA centers
sample times before fitting, reducing floating-point cancellation:

```text
x = sample_time - latest_sample_time
b = sum((x - mean(x)) * (weight - mean(weight))) / sum((x - mean(x))²)
predicted_time = latest_sample_time + mean(x) + (cut_target - mean(weight)) / b
```

EWMA describes learning **between shots**, not live scale filtering. For
captured offset `O`, target `G`, and final weight `W`, define error `e = W-G`
and effective compensation `z = O+e`. Reject nonfinite observations and
`abs(z) > 5 g` before smoothing. Otherwise:

| Mode | Next offset | Tradeoff |
| --- | --- | --- |
| Linear regression + offset correction | `clamp(O + e, 0, 5)` | Full correction reacts quickly and follows individual-shot noise. |
| Adaptive EWMA | `clamp(O + α*e, 0, 5)` | Smaller gains smooth noise; larger gains respond faster. |

With `O=1.50 g`, `G=36 g`, `W=36.20 g`, regression learns **1.70 g** and EWMA
at α=0.30 learns **1.56 g**. History retains **1.50 g** for that shot.
EWMA additionally requires the accepted post-drip measurement to remain fresh,
with valid baseline and connection provenance, following a normal weight-target
cut. Manual stops, time/safety limits, cup removal, excluded shots and Fast/Slow
extensions do not train it: forced stops do not measure normal cutoff error.
Existing regression eligibility is preserved.

Four candidate gains (0.10, 0.30, 0.50, 1.00) score compensation predictions
before updating them. After 20 eligible EWMA observations, evaluation occurs
every five observations. A challenger needs at least 10% lower squared-error
loss in two consecutive evaluations, and at least ten observations between
switches. The first possible switch is observation 25. Ties, insufficient
evidence and excluded shots retain the gain. Selection affects future shots
and never jumps the active offset to a candidate's offset. These are provisional
engineering constants, not an empirically optimal tuning policy.

EWMA v2 accepts any current α in hundredths from 0.01 to 1.00. A custom
incumbent is scored alongside those four candidates, using the same window
and switch criteria; it is not rounded to a candidate. Observations are replayed
from each trajectory's state before the oldest retained sample, without using
future measurements. Once a candidate wins, subsequent choices use the four
fixed gains. Neither saving a baseline nor switching the selector resets learning.

Each preset retains separate offsets. Switching to regression freezes EWMA evidence;
returning resumes it. Reboot retains offset, gain and initial/learned provenance,
but collects a fresh evidence window. Recipe or relevant tare/drip timing changes
also restart evidence without erasing the offset or gain.

The learned-offset readout previews the draft algorithm's own value. EWMA shows
α, its editable baseline and status alongside it; regression hides these fields. BBW OFF disables the
selector and hides learning fields. Polling preserves unsaved edits, and resets
require a saved selection/baseline and an editable, idle configuration.

- **Reset learned stop offset to baseline** resets only the selected mode's
  offset. For EWMA it retains α and restarts evidence.
- **Reset EWMA learning** restores EWMA's offset and α to their saved baselines,
  marks provenance initial, and clears evidence. Regression is retained.

Save both bases before resetting; editing them alone leaves learned values
and evidence intact. For the example above, α=0.10 gives a next offset of 1.52 g,
α=0.30 gives 1.56 g and α=0.60 gives 1.62 g. Higher α reacts faster but follows
shot noise more; lower α smooths more but adapts slower. The gain applies
between shots, not to live scale filtering. The saved base is a reset seed,
not a fixed gain that disables automatic adaptation.

Pending analysis cannot undo either reset. A future change to the firmware's
initial gain must preserve valid retained learning. Neither mode changes guard,
physical-stop, relay, watchdog or time-limit authority. No improved physical
accuracy is claimed without representative machine/scale measurements.

Related: [Cup protection](cup-protection.md), [Tare](../settings/tare.md),
[Scales](../settings/scales.md), [No-scale BBW](../settings/no-scale-bbw.md).
