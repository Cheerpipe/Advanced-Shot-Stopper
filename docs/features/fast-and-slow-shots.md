# Fast and slow shots

Every preset has a **Target yield** and a **Shot time window**, such as
27–36 s. When a shot reaches the target inside that window, it is
**On recipe** and stops at the target. A shot that gets there before the window
is **Fast**; one that gets there after it is **Slow**.

Fast shots are often a grind that is too coarse, slow shots a grind that is too
fine. On the Adaptive presets, ShotStopper softens both: a fast shot runs a few
grams longer, and a slow shot stops a few grams sooner. This cannot guarantee a
better-tasting cup; use a Fixed preset, or turn both options off, to always stop
at the target. To skip them for a few shots without changing the preset, turn
them off in the header's [guard panel](brew-by-weight.md#change-how-shots-stop-for-now).

## When it applies

Automatic brew by weight with a usable scale. Fast and slow handling does not
run when brew by weight is off, on rinses, or on manual shots without a scale.

Time is counted from the start of the shot, pre-infusion included. Drip
compensation applies to every stop, so the cup lands where the cut line says.
**Max shot time** and the 60-second safety limit always stop the shot, whatever
these options would do.

## The cut line

ShotStopper stops a shot when its weight meets the preset's **cut line**. The
line sits on the target inside the window. With the fast and slow options on,
it is higher before the window and lower after it, and it moves between those
levels gradually over 5 seconds instead of jumping. Reaching the target a
fraction of a second earlier or later therefore never changes your cup by more
than a fraction of a gram.

**Settings → Presets → How this preset cuts** draws this line for the selected
preset. Drag the example shot to see where a shot reaching the target at a
given time would stop.

The example shot and the colored zones begin when coffee usually starts
dripping for that preset, taken from its recent shots, so a single and a
double each keep their own timing. Until a preset has a shot with a recorded
first drop, the graph uses your other presets' usual time, or 0 s if no shot
has one yet; the note under the graph says which. **Relearn** starts this
timing over, while editing the window or the target keeps it.

## Fast shots

Turn on **Extend fast shots** (Settings → Presets → **Fast shots**) to let
a fast shot keep going past the target. The cut line starts at the target plus
**Extend by up to (g)**, then eases down to the target over the 5 seconds
before the window starts.

- A shot that is well ahead of the window stops at the full extra yield.
- A shot that is only a little early gets only a little more.
- Turned off, a fast shot stops at the target.

While a fast shot is running longer, its line reads
**Running fast · going to 38 g**, and the last shot card shows **EXTENDED**.
Home's shot card usually warns you earlier: with the default **By forecast**
colors it fades toward red while it gains confidence the shot will reach the
target before the window, and reads **Running fast** once it is sure.

## Slow shots

Turn on **Shorten slow shots** (Settings → Presets → **Slow shots**) to
stop a slow shot before the target. The cut line stays on the target until the
window ends, then eases down by up to **Shorten by up to (g)** over the next
5 seconds, where it stays.

- A shot that is only a little late loses only a little yield.
- A very slow shot stops at the full reduction, or at Max shot time if it never
  gets there.
- Turned off, a slow shot keeps going toward the target until Max shot time.

While a slow shot is stopping sooner, its line reads
**Running slow · stopping at 34 g**, and the last shot card shows
**STOPPED EARLY**.
Home's shot card reads **Past window** and turns blue as soon as the window
ends without a stop.

## Settings

These are saved with each preset under **Settings → Presets**. Defaults are
for the factory **Double · Adaptive** preset; [the others differ](presets.md#factory-recipes).
While you edit, the field result shows the resulting weight and ratio, such as
**→ up to 42 g (1:2.3)**.

| Setting | Default | Range | Effect |
| --- | --- | --- | --- |
| **Shot time window (s)** (Recipe) | 27 to 36 s (doubles), 23 to 32 s (singles) | **Window start** at least 2 s after Earliest weight stop; **Window end** at least 1 s after Window start and at least 1 s before Max shot time; 0.5 s steps | Reaching the target inside the window is On recipe; earlier is Fast, later is Slow. |
| **Extend fast shots** | ON | ON / OFF | Lets fast shots go past the target. |
| **Extend by up to (g)** | 6 g | 0.5–10 g in 0.1 g steps, and no more than a quarter of the target | The most a very fast shot gains over the target. |
| **Shorten slow shots** | ON | ON / OFF | Lets slow shots stop before the target. |
| **Shorten by up to (g)** | 3 g | 0.5–10 g in 0.1 g steps, leaving at least 5 g in the cup | The most a very slow shot stops short of the target. |

The extra and reduced yields are amounts relative to the target, so they move
with it when you change the target from Home or Settings. Fixed presets keep
these amounts saved but switched off, so they always stop at the target.

Once a preset has at least three shots, a line below the window such as
**Last 10 shots: 28–33 s, median 30.4 s** shows when its recent shots (up to
the last ten) reached the target. Changing the
window, or moving the target by more than 1 g, asks to start these shot
statistics over when you save.

## Channeling

If the flow suddenly surges during a shot — at least 3 g/s and half again
faster than a second earlier, seen on two readings in a row — ShotStopper
treats it as channeling. For the rest of that shot the cut line never rises
above the target, so a channeling shot is not run longer. The last shot card
shows **CHANNELING**, and the shot does not count toward the recent-shot
statistics, grind hints or the scale-loss backup.

## Grind hints

With **Grind hints** on (Recipe group, on by default), ShotStopper looks at
when your recent shots on the active preset reached the target, on the grind
setting you are using now, and compares their average with the middle of your
shot time window. It leaves out the fastest and the slowest of those shots, so
one odd shot does not steer it. A shot that runs into the max shot time, or that
you stop by hand after the window, counts as slow, so a choked shot still shows
up. When your shots run clearly fast or slow, Home shows a card:

- **Dialling in**: after **Relearn**. A single shot far outside the window is
  enough for a first suggestion, so a new bag gets advice from its first shot.
- **Grind hint**: the coffee was dialled in and has drifted, for example as the
  beans age. This card waits until several shots agree.
- **Checking your change**: your last shot moved the way the card suggested. No
  new advice until the next shot confirms the new setting.
- **Shots vary**: your shots spread more than this window allows. A grind
  change can't fix that; weigh each dose, check your puck prep, or widen the
  window.

The card says which way to go and roughly how far, in seconds only, for
example *Grind a touch coarser · about 3 s slow*; it says *at least* when one
of those shots hit the max shot time, was stopped by hand, or ran too far off
for an exact estimate. It also says how sure it is (**Early read**,
**Likely** or **Confident**) and from how many shots. A bar shows your window,
the comfortable middle of it and where your shots sit. The card never changes
a setting and never counts clicks or steps: only you know your grinder.

You don't need to tell ShotStopper when you adjust the grind; it notices the
change from your next shots and starts reading the new setting. Use
**Relearn**, on the card or in **Settings → Presets**, after a new bag or a
change to your dose, basket or grinder. **Ignore** hides the card in this
browser until the advice changes: another direction, a firmer suggestion or a
new grind setting brings it back.

## Examples

Factory Double · Adaptive: 36 g target, 27–36 s window, up to 6 g extra and
3 g less.

- 36 g reached at 30 s: On recipe, stops at 36 g.
- A shot that would reach 36 g at 24 s keeps running and stops near 40 g.
- A gusher reaching 36 g at 16 s stops at 42 g.
- A shot that would reach 36 g at 36 s stops a little earlier, near 35 g.
- A very slow shot stops once it reaches 33 g, or at the 50 s Max shot time.
- Scale lost during the shot: weight decisions pause, and the
  [scale-loss backup](auto-to-manual.md) and the time limits take over until
  the scale recovers.

[Single](presets.md#factory-recipes) uses smaller values. The final cup
includes dripping; these weights describe where the cut line stands, not a
guaranteed yield.

## Alerts

With a local buzzer, **Settings → Shared settings → Alerts → Fast-shot beeps**
pulses while a fast shot continues past its target, and **Slow-shot beeps**
pulses while a slow shot is being stopped sooner. The **Bullseye melody** plays only
after an On recipe shot. See [Alerts](../alerts.md).

Related: [Brew by weight](brew-by-weight.md), [Presets](presets.md),
[Shot history](shot-history.md).
