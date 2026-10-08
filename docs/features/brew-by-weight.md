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

The protection window affects when BBW may stop the machine. It does not
decide whether a completed cycle appears in [shot history](shot-history.md):
recording always requires more than 12 seconds and a valid final yield over
2 g, including for manual and timer-only endings.

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
(Manual). The **Presets** section below it summarizes how each recipe cuts as
three short rules — Fast, BBW, and Slow — built from that recipe's saved
settings; the active recipe arrives expanded.

| Setting | Default | Range | Effect on the shot |
| --- | --- | --- | --- |
| **Brew by weight** | ON | ON / OFF | ON: stop by weight when a scale is usable. OFF: paddle, **Stop**, and the 60 s firmware cap only. Fast, Slow, A→M, Max BBW time, and No-scale BBW become read-only. |
| **Stop after sustained weight** | ON | ON / OFF | A backup for a weight stop blocked by **Avoid accidental touch**. Fresh readings must stay above the applicable cut threshold for 1 second. Saved per preset; takes effect on the next shot. |
| **Target (g)** | 36 g | 10–200 g | Goal weight. Stop aims at `target − learned offset`. |
| **Max BBW time (s)** | 50 s | 5–60 s | Operational time limit for an **automatic BBW** cycle. Ignored on timer-only and no-scale shots. Cannot exceed the hard 60 s cap. |
| **Baseline offset (g)** | 1.5 g | 0–5 g | Seed used by **Reset learned stop offset**. Save this before reset. |
| **Learned stop offset** | starts at 1.5 g | 0–5 g | Subtracted from the target (and from Fast/Slow recovery weights). Updated from eligible BBW measurements after the drip delay. |
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

## Cutoff behavior and learning

### When touch protection delays a stop

**Stop after sustained weight** sits directly below **Avoid accidental touch**.
It only works when both options and BBW are on. If touch protection is off, the
backup keeps its saved ON/OFF selection but becomes read-only and has no effect.
Turning BBW off also makes it read-only immediately, without clearing its selection.
Turning touch protection back on restores that choice. New and reset recipes
start with the backup on; upgrading a supported earlier settings record also
turns the new option on without changing your touch-protection choice.

The normal weight stop does not wait an extra second. The backup starts counting
only when touch protection rejects weight that would otherwise allow a stop.
Fresh readings must remain above the applicable threshold for at least one second;
the weight may keep increasing. A brief spike, silence after one reading, a drop
below the threshold, a new tare or a connection interruption cannot complete that
interval. If touch protection releases sooner, normal stopping resumes immediately.

Fast still permits intentional extension: before its minimum time, the backup
uses the maximum recovery threshold. After that time it can use the normal
threshold. Slow uses its recovery threshold when recovery is applicable. The
learned offset applies as usual, and a change of threshold restarts the interval.
Delayed scale readings count only toward the threshold that applied when they arrived.

A finger held on the scale can also trigger this backup. It favors ending an
uncertain shot over continuing to wait; it does not prove that the reading is
coffee. Sampling, machine response and dripping mean the final yield can still
overshoot. Under-reported weight or vibration repeatedly crossing below the
threshold may prevent this backup from firing, so the existing time limits remain.
Backup endings appear as **Touch fallback** and do not train the learned offset
or the A→M duration trend.

### One cutoff algorithm

The former **Linear regression + offset correction** mode was removed; its
learned offset became your starting point. When you update from a firmware
that still had it, presets saved with that mode keep working: the offset the
machine actually used arrives as the EWMA starting offset — unless that
recipe already had learned EWMA values, which are kept as they were. The
first shot after the update runs with the migrated offset exactly as before;
learning resumes from the second shot on.

Every shot uses ten eligible samples, a positive linear trend, the existing
minimum prediction horizon and two-sample direct confirmation. Invalid
prediction falls back to direct stopping and the existing time limits. The
API and CSV still report `legacy` for shots recorded under the old mode; the
word describes past data, never a choice. Sample times are centered before
fitting, reducing floating-point cancellation:

```text
x = sample_time - latest_sample_time
b = sum((x - mean(x)) * (weight - mean(weight))) / sum((x - mean(x))²)
predicted_time = latest_sample_time + mean(x) + (cut_target - mean(weight)) / b
```

EWMA describes learning **between shots**, not live scale filtering. For
captured offset `O`, target `G`, and final weight `W`, define error `e = W-G`
and effective compensation `z = O+e`. Reject nonfinite observations and
`abs(z) > 5 g` before smoothing. Otherwise:

The next offset after an eligible shot is `clamp(O + α*e, 0, 5)`, with a
lower bound set by the connected scale's reporting lag: smaller gains smooth
noise; larger gains respond faster. With `O=1.50 g`, `G=36 g`, `W=36.20 g`,
α=0.30 learns **1.56 g**, and history retains **1.50 g** for that shot.
EWMA requires the accepted post-drip measurement to remain fresh, with valid
baseline and connection provenance, following a normal weight-target cut.
Manual stops, time/safety limits, cup removal, excluded shots and Fast/Slow
extensions do not train it: forced stops do not measure normal cutoff error.

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

Each preset retains its own offset. Reboot keeps offset, gain and
initial/learned provenance, but collects a fresh evidence window. Recipe or
relevant tare/drip timing changes also restart evidence without erasing the
offset or gain.

The learned-offset readout shows the offset with α, its editable baseline and
status alongside it. BBW OFF hides the learning fields. Polling preserves
unsaved edits, and resets require a saved baseline and an editable, idle
configuration.

- **Reset learned stop offset** restores the offset to the saved baseline. It
  retains α and restarts evidence.
- **Reset EWMA learning** restores offset and α to their saved baselines,
  marks provenance initial, and clears evidence.

Save both bases before resetting; editing them alone leaves learned values
and evidence intact. For the example above, α=0.10 gives a next offset of 1.52 g,
α=0.30 gives 1.56 g and α=0.60 gives 1.62 g. Higher α reacts faster but follows
shot noise more; lower α smooths more but adapts slower. The gain applies
between shots, not to live scale filtering. The saved base is a reset seed,
not a fixed gain that disables automatic adaptation.

Pending analysis cannot undo either reset. A future change to the firmware's
initial gain must preserve valid retained learning. Nothing here changes guard,
physical-stop, relay, watchdog or time-limit authority. No improved physical
accuracy is claimed without representative machine/scale measurements.

Related: [Cup protection](cup-protection.md), [Tare](../settings/tare.md),
[Scales](../settings/scales.md), [No-scale BBW](../settings/no-scale-bbw.md).
