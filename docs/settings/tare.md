# Tare

Three controls distinguish cup-placement tare outside a brew, shot-start tare,
and late-cup retare. Machine-level, under
**Settings → Machine and scale → Tare**, except **BBW protection**, which is
on the active brew preset.

Initial weight-stop protection is a separate brew setting; its range and
interactions are in [Cup protection](../features/cup-protection.md).

Together with [Cup](cup.md), this is the setting side of
[Tare and retare](../features/tare-retare.md) and
[Cup protection](../features/cup-protection.md).

## When it applies

Initial tare and timer commands can also run on timer-only shots with a usable
scale. Late retare and BBW protection apply to automatic BBW, not timer-only or
manual no-scale cycles. Shot **Post-tare grace** is inactive when **Automatic tare at shot start**
is off.

First-drop / first-flow detection still runs against the tare zero even while
the grace window is open. A small stream of coffee does not wait for the
scale to settle. Placing the cup (including overshoot around 150–200 g) or a
finger tap is not first drop and does not block retare.

For a cup placed after the retare window, stop and restart correctly; the
controller does not promise another automatic tare outside that window.

## Parameters

| Setting | Default | Range | Effect on the shot |
| --- | --- | --- | --- |
| **Automatic tare outside a brew** | ON | ON / OFF | Tare once on a new stable cup placement while idle, independently of shot-start tare, BBW, and cup protection. Never tares merely because a shot ends. |
| **Automatic tare at shot start** | ON | ON / OFF | Tare at shot start when a usable scale supports it, including timer-only shots. |
| **Post-tare grace (s)** | 2 s | 0.5–10 s | After a tare (start or late-cup retare), wait this long for ~0 g before using weight for **stop/control**. |
| **Late-cup retare during a shot** | ON | ON / OFF | Allow one late-cup retare during the retare window. Requires shot-start tare and fires on the cup-presence **placed** event. |
| **Retare window (s)** | 4 s | 0.5–10 s | Time after shot start to detect and retare a late-placed cup. |
| **Bookoo combined command** | ON | ON / OFF | Use the scale’s combined tare + start-timer command at shot start. Requires shot-start tare. Idle tare never starts the timer. Bookoo only. |

All tare switches are shared machine settings, not preset values. Save changes
while idle. **Automatic tare outside a brew** defaults ON on first setup, factory
reset, and upgrade from a configuration without this field. A saved OFF value
survives reboot and later upgrades. Enabling it with a cup already present does
not tare that cup.

## Outside a brew

After connection, let the controller observe an empty pan, then place the cup.
The existing [Cup](cup.md) stability settings qualify the placement and the empty
reference. After removal, let the empty pan stabilize before replacing the cup.
This triggers another tare, even if the cup contains coffee;
adding coffee or a spoon while the cup stays present does not.

The machine must be confirmed off. A new placement can tare after a normal
shot stop while the paddle is still ON; the paddle still must be released before
another shot. Rinse, active shots, uncertain stop state, safety failures, and
maintenance exclude idle tare. An excluded placement is not replayed later.

A cup detected before tare remains present at 0 g and satisfies **Require cup
to start**. Other start protections still apply. Boot/reconnect loses presence
evidence: a pre-tared cup at zero must be removed and replaced to be detected.
The integrated protocols currently do not report a verifiable physical-button
tare event; a zero reading alone cannot distinguish that action from removing
an untared cup.

A pending idle tare temporarily blocks a new start. Release the activator and
try again after the scale settles; the rejected gesture is not replayed. Queued
requests expire after 1 s; an executing write stays serialized until it returns.
The independent settling wait is bounded by the 1 s write allowance plus the
configured post-tare grace. Failed requests are not automatically retried on a
cup that remains present.

Before an idle write starts, fresh readings must still satisfy the configured
minimum using the original direct/relative reference and the placement stability
window. A changed or stale placement cancels the queued request;
remove and replace the cup to authorize another attempt. An executing write
keeps its ownership until it returns, even if the cup is removed or settings
change.

Write success alone does not confirm the cup's zero reference. The controller
checks notification capture order against the write boundary, including zero
readings received during the write. Buffered pre-write readings cannot confirm
the effect. If the bounded settling period ends without it, a fresh unchanged
reference is retained; otherwise the reference becomes uncertain and cannot
satisfy **Require cup to start**. A later unambiguous lift/replacement or a new
connection can establish fresh evidence; a zero alone cannot reconstruct
physical motion hidden by simultaneous tare.

Debug export schema 7 includes `idleTare`: request/placement IDs, eligibility
and terminal reasons, reference confidence, qualifying weight range, capture
and command times, sequence boundary, and dropped/rejected sample counts.
These fields come from the control snapshot. `effect_unconfirmed`, `unstable`,
`stale_sample`, `sample_gap`, and `removed` distinguish common incomplete
attempts. `qualificationMinG`/`qualificationMaxG` describe the observed window,
not configured minimum/maximum cup mass. `requestPlacementId` identifies the
originating placement even after a different cup replaces it. No periodic retry
is performed.

## Example

Shot-start tare is on, grace 2 s, retare window 4 s. The shot starts, the
scale tares, then you place the cup at 2 s. A second tare fires. Weight stop
still waits for BBW protection (default 12 s) so the cup weight cannot cut
the shot.

Related: [Tare and retare](../features/tare-retare.md),
[Cup protection](../features/cup-protection.md), [Cup](cup.md),
[Scales](scales.md).
