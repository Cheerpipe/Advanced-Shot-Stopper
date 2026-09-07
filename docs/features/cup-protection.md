# Cup protection

Cup protections handle missing cups, cup removal, and brief bumps. They apply
to automatic brew-by-weight shots. Configure recipe options in
**Settings → Brew** and shared detection/tare timing under
**Settings → Machine and scale**.

## Choose the behavior

| Control | Factory value | What it does |
| --- | --- | --- |
| **Enable cup protection** | On | Enables the cup-start/removal protections; also exposed in Home Quick Settings. |
| **Stop if cup is removed** | On | Requests a stop when a cup is detected as lifted after tare. Requires cup protection. |
| **Require cup to start** | Off | Blocks a start without a detected cup. Requires cup protection. If the scale was already tared before connecting, place the cup after connection so it can be detected. |
| **Avoid accidental touch** | On | Rejects brief implausible bumps from weight-stop and guard decisions. This is a separate brew option. |
| **BBW protection (s)** | 12 s | Blocks automatic weight stop at the beginning; first drops do not end the window. Maximum 30 s; minimum is enabled retare window + 3 s (3 s when retare is off). Must fit within Max BBW time. |

A cup-start refusal holds the relay open for that attempt. Release the physical
activator, correct the cup/scale condition, then activate again. A blocked
held button is not forwarded halfway through the hold.

## How timing fits together

At shot start, the controller can tare immediately. If you place the cup within
the retare window, a second tare can run once without restarting the timer.
After each tare, a settle interval protects the weight readings. Independently,
BBW protection delays weight-based stopping from the beginning of the shot.

Use [Tare settings](../settings/tare.md) for timing values and
[Cup settings](../settings/cup.md) for detection thresholds. Timer-only and
manual no-scale cycles skip late retare and initial BBW protection.

## Examples

- **Cup already in place:** start tare runs; a stable cup does not cause another
  tare just because it remains on the pan.
- **Cup placed at 2 s:** with the default 4 s retare window, stable placement
  triggers one retare. Weight stop remains blocked until the 12 s protection
  window ends.
- **Cup placed after the retare window:** no automatic late tare is promised.
  Stop and restart with the cup correctly placed rather than interpreting its
  mass as coffee.
- **Cup lifted during brewing:** with cup protection and removal stop enabled,
  the controller requests stop. The physical stop mechanism depends on the
  [machine type](../../README.md#machine-types).

Do not press the scale with a finger to cancel a shot: touch protection can
ignore that load. Use the physical stop action for your
[paddle mode](../settings/paddle.md) or [button](../settings/momentary.md).

Related: [BBW](brew-by-weight.md), [tare and retare](tare-retare.md).
