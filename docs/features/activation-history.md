# Activation history

The activation history is a simple, independent diary of every confirmed
machine activation: shots, rinses, and short runs that never became shots.
It answers "what did the machine actually do, and when?" without the
measurement detail of the [shot history](shot-history.md).

## What is recorded

Every activation the machine confirmed is recorded the moment its circuit
opens, including manual brews and rinses that the shot history skips. Each
entry keeps the local time, the duration, a type, and whether the scale
registered weight during the activation:

- **Shot** — the activation outlasted the brew-by-weight protection window
  (12 s with default settings). This is the same minimum the shot history
  and Home's last-good shot use, so the three views agree on what counts as
  a shot.
- **Rinse** — a quick rinse cycle.
- **Other** — a confirmed activation that ended at or before the protection
  window, such as a brief paddle blip or an aborted brew.

Abandoned starts, where the machine never confirmed the activation, record
nothing. When the controller's clock has never synced, cards show "no time"
until it gets the time from the network.

The log holds up to **1000** activations. When it fills, the oldest entry is
dropped to make room for the newest. Entries are written to memory the
instant a cycle ends and saved to flash a moment later when the controller
is idle, so recording never slows down a brew.

## In the Web UI

Open the **History** page (next to Stats) to browse the diary. The list loads
**20 entries at a time** as you scroll and refreshes every 20 s. Sort by date,
newest or oldest first, from the sort control at the top. Each card leads with
the duration large on the left and the friendly time label small on the right:
"Today" or "Yesterday" with the clock time to the minute, the weekday name for
the rest of the week, "2 weeks ago" for older weeks, and a short date like
"Sep 15" for older entries. Hovering the label shows the exact date and time.
The activation type sits below as a small label. Shots carry a coffee-cup
icon on the left of the card: a filled cup when the scale registered the
weight while brewing, and an outlined cup when no weight was registered,
such as a manual brew without a scale. Rinses carry a droplet, and other
activations carry a lightning bolt. When the
clock was not synced when the entry was recorded, the card shows "no time"
instead of a date.

Delete a single entry with the ✕ on its card, or clear the whole diary with
the Clear button. Clearing asks for an explicit confirmation and cannot be
undone.

## Stats vs History

The two pages record different things and never substitute for each other:

| | Stats (shot history) | History (activation diary) |
| --- | --- | --- |
| Records | Automatic brew-by-weight shots with a settled weight of at least 1 g | Every confirmed activation, including rinses and manual brews |
| Minimum duration | Longer than the BBW protection window | Longer than the BBW protection window to count as a shot |
| Detail | Goal, yield, error, flow, guards, rating, curve | Time, duration, type |
| Capacity | 100 shots | 1000 activations |

A good shot appears in both. A rinse or a short activation appears only
here. Deleting or clearing in one page never touches the other.

USB: see [USB serial CLI](../SERIAL_CLI.md) for factory reset, which clears
both logs.

Related: [Shot history](shot-history.md), [Brew by weight](brew-by-weight.md),
[Wi-Fi](../settings/wifi.md) (timezone for time labels).
