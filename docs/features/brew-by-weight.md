# Brew by weight

Automatic brew-by-weight (BBW) stops the shot where its weight meets the
recipe's **cut line**, minus the learned drip. The cut line is your target
yield while the shot is on recipe; on the Adaptive presets it rises a little
for shots that run fast and drops a little for shots that run slow, always
smoothly. See [Fast and slow shots](fast-and-slow-shots.md). It is **on by
default**.

Turn it off if you want the paddle (or switch) and the firmware 60 s cap
only. Tare and the shot timer still run; weight stop, cup protection at
start, automatic retare, Max shot time, and drip learning do not.

## When it applies

BBW runs on an automatic shot that started with a usable scale. It does not
run on a rinse, a timer-only shot (BBW off), or a manual no-scale shot.

Once the selected paddle mode permits weight stopping and the start-of-shot
protection window ends (see
[Cup protection](cup-protection.md)), two fresh scale samples at or above
`cut line − learned drip` can request a stop. A prediction from the recent
flow usually stops a moment earlier, so the coffee still in flight and the
drip land on the cut line. The learned drip is capped at 5.0 g and can be
reset to its starting value from the Web UI.

The protection window affects when BBW may stop the machine. It does not
decide whether a completed cycle appears in [shot history](shot-history.md):
recording always requires more than 12 seconds and a valid final yield over
2 g, including for manual and timer-only endings.

Automatic brew-by-weight cycles are limited by **Max shot time** and a
firmware hard cap of **60 seconds**. Timer-only (BBW off) and manual
no-scale shots skip Max shot time. In Original paddle mode, weight stop and the
operational wall are held off
while the paddle stays ON; the 60 s electrical cap remains. Momentary stop
pulses depend on machine-state confirmation: see
[Stopping and time limits](../settings/momentary.md#stopping-and-time-limits).

## Parameters

Defaults below describe factory Double; see [Presets](presets.md) for Single.
These are saved with each preset under **Settings → Presets**, except where
noted. While you edit, values that conflict with another field are flagged
in yellow right away — the same message Save would show — so you can fix
them before saving. **Home → Quick Settings → Brew by weight** turns brew by
weight on or off for the session (Manual) without changing the preset.

| Setting | Group | Default | Range | Effect on the shot |
| --- | --- | --- | --- | --- |
| **Brew by weight** | Recipe | ON | ON / OFF | ON: stop by weight when a scale is usable. OFF: paddle, **Stop**, and the 60 s firmware cap only; the fast and slow options, the scale-loss backup, Max shot time, and No-scale BBW become read-only. |
| **Dose (g)** | Recipe | 18 g | 5–30 g in 0.1 g steps | Coffee in the basket. Shows your ratio; does not change the cut. |
| **Target yield (g)** | Recipe | 36 g | 10–200 g in 0.1 g steps | Drink weight in the cup, drips included. The stop aims at `cut line − learned drip`. |
| **Shot time window (s)** | Recipe | 27–34 s | See [Fast and slow shots](fast-and-slow-shots.md#settings) | Reaching the target inside the window is On recipe. |
| **Earliest weight stop (s)** | Shot limits | 12 s | 0.5–30 s; see [Cup protection](cup-protection.md) | The scale cannot stop the shot before this, so tare and first drops cannot trigger it. |
| **Max shot time (s)** | Shot limits | 50 s | 5–60 s | The shot always stops here, target reached or not. Ignored on timer-only and no-scale shots. Cannot exceed the hard 60 s cap. |
| **If the scale drops, stop at (s)** | Shot limits | empty | Earliest weight stop to Max shot time | Leave empty to use your recent shots, or the middle of your window until there are enough. See [scale-loss backup](auto-to-manual.md). |
| **Learned drip (g)** | Drip compensation | starts at 1.5 g | 0–5 g, read-only | Subtracted from the cut line. Updated from eligible weight-stopped shots after the drip delay. |
| **Starting drip (g)** | Drip compensation | 1.5 g | 0–5 g | The drip restored by **Reset drip learning**. Save it before resetting. |
| **Learning speed** | Drip compensation | 0.30 initially | Read-only | How strongly each shot moves the learned drip, with whether it is still the starting value or learned, and its collecting/evaluating status. |
| **Starting learning speed** | Drip compensation | 0.30 | 0.01–1.00, step 0.01 | Restored by **Reset drip and learning speed**. Saving it keeps the current drip, speed and evidence. |
| **Stop after sustained weight** | Shared settings → Cup protection | ON | ON / OFF | A backup for a weight stop blocked by **Avoid accidental touch**. Fresh readings must stay above the applicable cut threshold for 1 second. Applies to every preset; takes effect on the next shot. |

**Home → Presets** shows how each recipe cuts as three rows, for example
**Before 27 s · Fast · up to 40 g**, **27–34 s · On recipe · 36 g** and
**After 34 s · Slow · down to 33 g**; a side that is switched off reads
**off**. The active recipe arrives expanded.

Fixed behavior (not separate settings):

- Direct stop: two fresh samples at the threshold after Earliest weight stop.
- Predictive stop: from the flow of the last 3 seconds, it stops when the
  weight plus the coffee still in flight will meet the cut line.
- Scale loss: weight control pauses; physical stop behavior and applicable time
  limits stay in force.
  See [scale-loss backup](auto-to-manual.md).

## Example

Double · Adaptive at 36 g, learned drip 1.5 g, shot time window 27–34 s.
A shot reaching the target inside the window stops with about **34.5 g** on
the scale, so the post-drip weight lands near 36 g. A shot running early gets
a slightly higher cut line (up to 40 g), one running late a slightly lower one
(down to 33 g); see [Fast and slow shots](fast-and-slow-shots.md).

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

The backup uses the cut line at each reading, minus the learned drip, so a
fast shot can still run to its raised cut line and a slow one stops at its
lowered line. When the cut line moves from one part to another (raised,
target, lowered) the interval restarts. Delayed scale readings count only
toward the part of the line that applied when they arrived.

A finger held on the scale can also trigger this backup. It favors ending an
uncertain shot over continuing to wait; it does not prove that the reading is
coffee. Sampling, machine response and dripping mean the final yield can still
overshoot. Under-reported weight or vibration repeatedly crossing below the
threshold may prevent this backup from firing, so the existing time limits remain.
Backup endings appear as **Touch fallback** and do not train the learned drip
or the scale-loss backup's recent shots.

### One cutoff algorithm

The former **Linear regression + offset correction** mode was removed. Moving
a device from a firmware that still had it is a fresh start: the installation
uses `--erase-all`, saved recipes and shot history are not carried over, and
brew-by-weight begins from the factory EWMA defaults.

Every shot uses the smooth cut: a flow estimate `q` from a least-squares fit
over the accepted samples of the last 3 seconds (at least four samples over at
least 2 seconds, with outlying readings excluded), and the earliest stop time
`t*` where

```text
weight + q × (t* − t_sample) + q × τ + offset ≥ cut_line(t*)
```

`τ` is the scale's reporting lag plus 0.1 s for the valve (plus the stop pulse
on push-button machines). Without a usable flow (below 0.3 g/s, or not enough
samples) the shot stops by two-sample direct confirmation, and the existing time
limits always apply.

EWMA describes learning **between shots**, not live scale filtering. For
captured offset `O`, the cut line at the stop `G`, and final weight `W`, define
error `e = W-G`
and effective compensation `z = O+e`. Reject nonfinite observations and
`abs(z) > 5 g` before smoothing. Otherwise:

The next offset after an eligible shot is `clamp(O + α*e, 0, 5)`, with a
lower bound set by the connected scale's reporting lag: smaller gains smooth
noise; larger gains respond faster. With `O=1.50 g`, `G=36 g`, `W=36.20 g`,
α=0.30 learns **1.56 g**, and history retains **1.50 g** for that shot.
EWMA requires the accepted post-drip measurement to remain fresh, with valid
baseline and connection provenance, following a weight cut on any part of the
cut line. Manual stops, time/safety limits, scale-loss stops, cup removal,
detected channeling and excluded shots do not train it: forced stops do not
measure normal cutoff error.

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

In **Settings → Presets → Drip compensation**, **Learned drip** shows the
offset and **Learning speed** shows α with its status; **Starting drip** and
**Starting learning speed** are their editable baselines. BBW OFF hides the
learning fields. Polling preserves unsaved edits, and resets require a saved
baseline and an editable, idle configuration.

- **Reset drip learning** restores the offset to the saved Starting drip. It
  retains α and restarts evidence.
- **Reset drip and learning speed** restores offset and α to their saved
  starting values, marks provenance initial, and clears evidence.

Save both starting values before resetting; editing them alone leaves learned values
and evidence intact. For the example above, α=0.10 gives a next offset of 1.52 g,
α=0.30 gives 1.56 g and α=0.60 gives 1.62 g. Higher α reacts faster but follows
shot noise more; lower α smooths more but adapts slower. The gain applies
between shots, not to live scale filtering. The saved base is a reset seed,
not a fixed gain that disables automatic adaptation.

Pending analysis cannot undo either reset. A future change to the firmware's
initial gain must preserve valid retained learning. Nothing here changes guard,
physical-stop, relay, watchdog or time-limit authority. No improved physical
accuracy is claimed without representative machine/scale measurements.

Related: [Fast and slow shots](fast-and-slow-shots.md),
[Cup protection](cup-protection.md), [Tare](../settings/tare.md),
[Scales](../settings/scales.md), [No-scale BBW](../settings/no-scale-bbw.md).
