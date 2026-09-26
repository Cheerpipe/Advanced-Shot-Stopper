# Tare and retare

Three main settings control automatic zeroing: **Automatic tare outside a brew**
on a new cup placement, **Automatic tare at shot start**, and **Late-cup retare
during a shot** if you place the cup after the shot starts. An optional fourth
switch, **Retare when adding or removing an accessory**, extends only the
outside-brew behavior.

All four are **on by default**. Late retare also requires shot-start tare.

## Automatic tare outside a brew

With the machine confirmed off, a qualified stable empty pan followed by a stable
placement triggers one tare using the same cup detector as late retare. It
does not start the timer. A cup that remains on the scale after a shot never
triggers this tare, even after the beverage becomes stable. Removing and
replacing that full cup does trigger it.

**Home → Cup → Automatic tare** shows readiness and what to do when tare cannot
proceed. If you switch on with a cup already showing a positive weight, remove
it, let the empty scale settle near zero, then replace it. If the cup was already
tared to zero before connecting, let the controller see its steady zero, then
remove it. A continuous stable negative reading can become the empty reference;
placing a **different-weight** cup then triggers tare. If readings were lost, the
replacement reads near the original zero, or the scale started substantially
negative, use **Diagnostic** to tare the empty scale, wait for stable zero, then place the cup.
Weight readings alone cannot distinguish an equal-weight replacement from the
scale being moved and returned.
If the scale briefly dips well below its eventual empty reading, the controller
may likewise need that empty-pan diagnostic tare: the rebound could also be a
new cup. Let the empty pan settle before replacing the cup.

Once an empty reference is known, two consecutive fresh readings showing
near-total unloading can authorize a stable replacement without another stable
empty-pan pause. This works for lighter, equal, and heavier replacements,
including negative tare offsets. An empty-pan rebound never counts as a cup;
the detector reuses the known reference, not the removal minimum. Without that
reference or sufficient unloading evidence, remove the cup, let the empty pan
settle, and replace it. A swap hidden between notifications cannot be guaranteed.
See [Cup settings](../settings/cup.md#fast-replacement-outside-a-shot) for the
unloading and stability requirements. A stable load change alone does not
trigger tare with the accessory option OFF.

If you use a chilling bar or another accessory, keep the accessory option on
under **Automatic tare outside a brew**. Let the cup auto-tare to zero, then
add the accessory before brewing and wait for its added weight to stabilize.
The controller tares one such addition per cup placement. Removing that known
accessory before brewing causes another tare after its weight settles, while
the cup remains present. If cup and accessory arrived together, or their
separate weights are too similar, remove everything and let the empty pan settle
before placing the cup again. The same weight change during a shot follows the
existing shot behavior, and a full cup
settling after the shot does not trigger accessory retare. A stable addition of
coffee is indistinguishable from an accessory, so leave the option OFF if that
could happen during preparation. See [Tare settings](../settings/tare.md#outside-a-brew)
for prerequisites and limits.

The empty reference remains fixed across larger disturbances and after the
first cup placement. A stable negative excursion followed by a return near zero
does not represent an added cup. Let the empty reference stabilize before the
next placement. Small, stable negative shifts before the first cup can qualify
as an empty pan.
Larger initial negative offsets without a preceding qualified zero cannot
authorize relative placement; recovery is described in [Cup settings](../settings/cup.md).

A known tared cup remains present at 0 g, so **Require cup to start** accepts
it. If shot-start tare is enabled, starting the next shot still performs its
normal tare. If that start races the idle tare, the accepted paddle or button
gesture starts immediately: queued idle work is canceled, and a write already
in progress may finish without entering the new shot. You do not need to release
and retry. Other start protections remain authoritative. See
[Tare settings](../settings/tare.md#outside-a-brew) for idle prerequisites,
pending-command behavior, and reconnect/physical-tare limitations.

Placement stability limits the total spread across the observed window, and
fresh contradictory readings cancel an idle tare that has not started writing.
Replacing a cup with a lighter one can leave a negative net reading; that
occupied reference stays valid until an actual further lift or tare changes it.
Valid negative removal readings from a previously accepted heavy load still
reach the cup detector; this does not expand the placement or brew weight limits.
If a much lighter replacement remains below the supported −500 g net-reading
limit, tare the empty pan from **Diagnostic**, wait for stable zero, then place it.

Observed removal during drip analysis preserves the previous shot's captured
last-known weight and skips post-drip learning, even with idle tare disabled.
A single credible near-empty reading can discard that optional analysis;
it cannot authorize a replacement tare. A completely unsampled swap remains
indistinguishable from some additions to the same cup.

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
and BBW protection. Shot post-tare grace is inactive when **Automatic tare at shot start** is off.

## Parameters

Configured under **Settings → Machine and scale → Tare** (and **Cup** for
presence thresholds). Full tables: [Tare](../settings/tare.md),
[Cup](../settings/cup.md).

## Example

Cup placed while idle: placement tare → cup reads 0 g and remains present →
shot starts → shot-start tare → brew continues.

Cup placed two seconds after paddle ON: start tare runs, then cup detection
fires a second automatic tare inside the 4 s window. The shot timer does
not restart; weight stop stays blocked until BBW protection ends.

If you place a cup after the 4 s default window, automatic retare is no longer
available for that placement. If **Automatic tare at shot start** is off, prepare the scale's
zero yourself; do not assume post-tare settling protection is active.
[Require cup to start](cup-protection.md) is an alternative when you always
want the cup in place before brewing.

Related: [Cup protection](cup-protection.md), [Brew by weight](brew-by-weight.md),
[Tare](../settings/tare.md), [Cup](../settings/cup.md).
