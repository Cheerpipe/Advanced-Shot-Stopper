# Activation history

The activation history is a simple, independent diary of every confirmed
machine activation: shots, rinses, and short runs that never became shots.
It answers "what did the machine actually do, and when?" without the
measurement detail of the [shot history](shot-history.md).

## What is recorded

Every activation the machine confirmed is recorded the moment its circuit
opens, including manual brews and rinses that the shot history skips. Each
entry keeps the local time, the duration, and a type:

- **Shot** — the activation outlasted the brew-by-weight protection window
  (12 s with default settings). This is the same minimum the shot history
  and Home's last-good shot use, so the three views agree on what counts as
  a shot.
- **Rinse** — a quick rinse cycle.
- **Other** — a confirmed activation that ended at or before the protection
  window, such as a brief paddle blip or an aborted brew.

Abandoned starts, where the machine never confirmed the activation, record
nothing. When the wall clock has never synced, the time column shows "—"
until the controller gets time from the network.

The log holds up to **1000** activations. When it fills, the oldest entry is
dropped to make room for the newest. Entries are written to memory the
instant a cycle ends and saved to flash a moment later when the controller
is idle, so recording never slows down a brew.

## In the Web UI

Open the **History** page (next to Stats) to browse the diary. The list loads
**20 entries at a time** as you scroll and refreshes every 20 s. Sort by date,
newest or oldest first, from the sort control at the top. Each card shows the
duration large, the type as a badge, and the local time; the timestamp falls
back to "—" when the clock was not synced when the entry was recorded.

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
