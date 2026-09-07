# Cup

Thresholds that decide whether a cup is **on the scale** or **lifted**. Used
by late-cup retare and start-of-shot protection. Machine-level, under
**Settings → Machine and scale → Cup**.

How those detections protect the shot is explained in
[Cup protection](../features/cup-protection.md).

## When it applies

Automatic brew-by-weight. A stable load at or above the minimum cup weight
counts as **placed**. A confirmed weight at or below the removed threshold
counts as **lifted**. Placement also requires a short run of stable samples.

## Parameters

| Setting | Default | Range / notes | Effect on the shot |
| --- | --- | --- | --- |
| **Minimum cup weight (g)** | 10 g | 1–500 g | Stable load that counts as a cup being placed. Shared detection threshold used by first-flow and retare. |
| **Cup-removed threshold (g)** | −3 g | −50 to −0.1 g | Confirmed weight at or below this means the cup was lifted. |
| **Placement samples** | 3 | 2–10 | Number of stable samples required before cup-present. |
| **Placement tolerance (g)** | 2.0 g | 0.1–20 g | How much those samples may differ. |
| **Max sample gap (s)** | 0.5 s | 0.1–5 s | Maximum gap between those samples. |
| **Min stable time (s)** | 0.3 s | 0–2 s | Minimum time the load must stay stable. |

## Example

You set a cup down during the retare window. After three samples within 2 g
of each other, lasting at least 0.3 s, the firmware treats it as placed and
can retare. After tare, lifting the cup can produce a negative reading; a confirmed
reading at or below −3 g counts as removed. The removal-stop option is
configured separately in [Cup protection](../features/cup-protection.md).

If a light cup is below the configured minimum, placement will not qualify.
Choose a threshold below its stable empty weight, within the allowed range.
Do not change stability parameters to compensate for an unstable surface.
Stable time must also fit inside the retare window and cannot exceed placement
samples × maximum sample gap. The server rejects inconsistent combinations.

Related: [Cup protection](../features/cup-protection.md),
[Tare and retare](../features/tare-retare.md),
[Tare](tare.md).
