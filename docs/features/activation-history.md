# Activation history

The activation history is a simple, independent diary of every confirmed
machine activation: shots, rinses, short runs, and recognized machine wake
gestures.
It answers "what did the machine actually do, and when?" without the
measurement detail of the [shot history](shot-history.md).

## What is recorded

Every activation the machine confirmed is recorded when its circuit opens,
including manual brews and rinses that the shot history skips. Each
entry keeps the local time, duration, and activation type:

- **Shot** — an activation that outlasted the brew-by-weight protection
  window (12 seconds by default). The label records the intention to brew,
  even when no measured shot is saved in [Stats](shot-history.md).
- **Rinse** — a quick rinse cycle.
- **Other** — a confirmed activation that ended within the protection
  window or exactly at its end, such as a brief paddle blip.
- **Power ON** — a paddle gesture recognized as a Linea Micra standby wake.
  It records how long the paddle kept the wake circuit active but never counts
  as a shot in Stats.

Abandoned starts, where the machine never confirmed the activation, record
nothing. When the controller's clock has never synced, cards show "no time"
until it gets the time from the network.

A recognized Linea Micra standby wake creates one **Power ON** entry when the
paddle returns to OFF. See
[Linea Micra](../settings/linea-micra.md#recognize-paddle-wake-gestures).

The log holds up to **1000** activations. When it fills, the oldest entry is
dropped to make room for the newest. Entries enter memory when a cycle ends.
Flash saving follows when the controller is idle, so recording never slows down
a brew.

## In the Web UI

Open the **History** page (next to Stats) to browse the diary. The list loads
**20 entries at a time** as you scroll and refreshes every 20 s. Sort by date,
newest or oldest first, from the sort control at the top. Each card leads with
the duration large on the left and the friendly time label small on the right:
"Today" or "Yesterday" with the clock time to the minute, the weekday name for
the rest of the week, "2 weeks ago" for older weeks, and a short date like
"Sep 15" for older entries. Hovering the label shows the exact date and time.
The activation type sits below as a small label. Shots carry a coffee-cup
icon on the left of the card. Rinses carry a droplet, and other
activations carry a lightning bolt, and Power ON entries use the power symbol.
When the
clock was not synced when the entry was recorded, the card shows "no time"
instead of a date.

Delete a single entry with the ✕ on its card, or clear the whole diary with
the Clear button. Clearing asks for an explicit confirmation and cannot be
undone.

## Stats vs History

The two pages record different things and never substitute for each other:

| | Stats (shot history) | History (activation diary) |
| --- | --- | --- |
| Records | Brews with a valid settled yield over 2 g | Every confirmed activation, including rinses and manual brews |
| Minimum duration | Longer than 12 seconds | Longer than the configured BBW protection window to carry a Shot label |
| Detail | Goal, yield, error, flow, guards, rating, curve | Time, duration, type |
| Capacity | 100 shots | 1000 activations |

A measured shot appears in both. A longer activation without a valid yield
can appear as Shot here while staying out of Stats. Deleting or clearing in
one page never touches the other.

USB: see [USB serial CLI](../SERIAL_CLI.md) for factory reset, which clears
both logs.

Related: [Shot history](shot-history.md), [Brew by weight](brew-by-weight.md),
[Wi-Fi](../settings/wifi.md) (timezone for time labels).
