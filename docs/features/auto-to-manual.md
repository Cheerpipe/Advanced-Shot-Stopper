# Scale-loss backup

The scale-loss backup (formerly the A→M time guard) protects brew-by-weight
shots. If the scale drops out mid-shot, ShotStopper can no longer stop on weight, so it picks the best stop
time it can and stops the shot then, unless the scale comes back first.
**On by default.**

A→M means auto-to-manual: weight stop is paused, but the shot keeps running
until the backup time. Paddle builds open the relay; momentary builds request a
stop pulse subject to
[machine-state and safety conditions](../settings/momentary.md#stopping-and-time-limits).

## When it applies

Every brew-by-weight shot that started with the scale connected. It does
**not** apply to shots that start as manual (`MANUAL_NO_SCALE`), timer-only
(BBW off), or rinses.

The scale counts as lost when Bluetooth drops or **weight notifications stop
arriving** for about a second. A steady weight is not a loss: fresh readings
still count even if the brew logic ignores one. ShotStopper keeps trying to
reconnect for the **whole** shot. If the scale returns with three good
readings, weight stop resumes and the backup time is cancelled. If the scale
drops again later, a new backup time is chosen from what is known then.

## Choosing the backup time

ShotStopper uses the first of these that applies:

1. **Fixed time.** If the preset has a time in **If the scale drops, stop at
   (s)**, the shot stops at that time.
2. **The shot was about to stop.** If the shot had already reached its cut
   line, it stops right away.
3. **The flow it just saw.** If the coffee was flowing steadily and would have
   reached the cut line within the next 3 seconds, the shot stops when it
   would have.
4. **Your recent shots.** With at least three recent shots on this preset, the
   shot stops a little after their typical finish time, never later than the
   end of your window.
5. **Your window.** Otherwise, the shot stops in the middle of your shot window.

The backup time is never earlier than the last reading the scale sent or the
weight-cut delay, and never later than the max shot time. A scale that drops
before the first drops of coffee uses your recent shots or your window.

## Parameters

| Setting | Default | Range / notes | Effect on the shot |
| --- | --- | --- | --- |
| **Finish by time if the scale drops** (**Settings → Shared settings → Scale-loss backup**) | ON | ON / OFF, shared by every preset; also **Home → Stop the shot → Protections → Scale-loss backup**, which asks before turning it off | OFF: if the scale never returns, the shot runs until paddle OFF or the machine circuit / max shot time. |
| **If the scale drops, stop at (s)** (**Settings → Presets → Shot limits**) | empty | Empty, or Earliest weight stop … Max shot time, per preset | Empty picks the backup time as described above. A time always stops the shot then. |

While brewing by weight, its row under **Protections** shows when the next shot would
stop if the scale dropped before the coffee starts flowing, for example
**Stops at ~31.5 s if the scale drops**.

The Diagnostic page's guard panel shows `Off` / `Idle` / `Armed` / `Scale lost · Ns`. Shot history records
`stop_detail = auto_to_manual` and `cut_type = limit`, along with how the
backup time was chosen.

## Recent shots

Recent shots are the newest ten weight-stopped shots on the same preset.
Shots stopped by this backup, by the max shot time or by hand do not count,
and neither do rinses or shots where the coffee channeled.

When you change beans or grind, tell ShotStopper so old shots stop counting:
use **New coffee** in **Settings → Presets** or on Home's grind-hint card (or
the integration API's reset-guard-samples call).
Changing the preset's window or moving its target by more than 1 g starts
fresh automatically.

## Example

An automatic Double with a 27–34 s window loses Bluetooth at 12 s, before the
coffee is near its target. The last three shots finished around 30 s, so the
backup time is about 31.5 s. ShotStopper keeps reconnecting; if the scale comes
back at 20 s with three good readings, weight stop resumes and the backup time
is cancelled. If the scale is lost at 29 s while the coffee is flowing toward
the target, the shot stops when the flow says it would have reached it.

Related: [Brew by weight](brew-by-weight.md), [Presets](presets.md),
[Alerts](../alerts.md) (Scale-loss backup ended a shot).
