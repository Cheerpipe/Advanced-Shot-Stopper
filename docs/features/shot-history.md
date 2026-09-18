# Shot history

Automatic brew-by-weight shots with a connected scale are stored on the
controller and shown in the Web UI shot log. Use it to see why a shot ended
and how close it landed to the target.

## What is recorded

Only **automatic BBW** shots qualify: scale present at start, brew-by-weight
enabled (not timer-only), and automatic weight control active. Manual shots,
timer-only brews, and cycles without a scale are **not** stored.

Typical fields include local time (from the configured timezone offset),
duration, the exact preset name captured at shot start, goal and yield (the
shot's actual output), error, average flow, first-drop time, whether Fast/Slow
guards ran or extended the shot, `shot_type`, `cut_type`
(`auto`, `manual`, `limit`), `stop_detail` (for example
`normal_target`, `activator`, `web_stop`, `wall_limit`, `hard_limit`,
`extended_max_weight`, `cup_removed`), and a manual `rating` from 0
(unrated) to 5. Rate a stored shot from its history card. The same stars are
available on Home's **Current / Last Good Shot** card only while that aggregate's
exact history row still exists; tapping the current star again clears the score.
The preset snapshot is also shown on that Home card and on every Stats history
card. Renaming or deleting a preset later does not rewrite a shot's displayed
name.

The log holds up to **100** shots. The following are never stored:

- Quick rinses and cycles that do not outlast the brew-by-weight protection
  window (12 s with default settings)
- Manual, timer-only, or no-scale shots
- Shots whose final weight is missing or below 1 g (e.g. scale off the
  machine or disconnected)

Those empty or sub-1 g shots are not written to history, used in averages, or
used for learned stop offset or A→M samples. They also do not replace Home's
idle last-good aggregate. During a live cycle, Home still shows that current
cycle.

Curve samples and the history record are written **once** when the cycle
closes (after the configured drip delay), not during an active brew. Shot
duration and the curve time axis end when the machine circuit opens. The
settled post-drip weight replaces the curve's endpoint at that same end time;
the drip-delay interval is not appended to the graph. Before the first drop,
the weight chart shows a dark green outline along the zero-weight axis, and
the flow chart shows an aqua outline along its zero-flow axis, with no shaded
area. An aqua drop with only the first-drop time beside it marks that moment on
the weight chart only. The time axis shows only its fixed 10-second labels;
Fast, Slow, and A-to-M changes remain visible through the curve colors without
adding competing time labels. Curves without a first-drop event keep their full
available grid. The current-shot curve exposed to Home is an in-memory view;
when idle, Home loads a saved curve only by the last-good aggregate's exact
history ID. It never falls back to the newest curve. Neither is a persistent
live-telemetry service.

Observed removal or a new placement during the drip delay preserves the weight
captured at shot end and discards post-drip learning, regardless of the idle
automatic-tare setting. A single fresh reading consistent with the known empty
pan is enough to discard this optional analysis, but cannot trigger a replacement
tare. If a physical swap is completely absent from the scale notifications,
weight alone cannot establish that the final reading belongs to another cup.

## In the Web UI

Open the shot history table to browse rows, delete one entry, clear the
whole log, or export CSV. Clearing requires an explicit confirm.

History is not Home's source of truth. Deleting the row or clearing the log
never substitutes another shot into **Current / Last Good Shot** and does not
erase its measurements. It only removes the optional saved curve and disables
rating there. Factory reset clears both history and the durable shot aggregates.
The shot history also does not feed the [activation history](activation-history.md):
that diary records every confirmed activation, while this log keeps only
qualifying automatic shots. Deleting or clearing here never changes that page.

Sort the list by **Date** or **Rating**, ascending or descending. Date
defaults to newest first. Rating puts unrated shots (0 stars) at the end
in both directions; equal scores keep newer shots first. The table loads
**10 shots at a time** as you scroll. The 20 s poll refreshes only the
first page of the current sort so new shots appear without re-downloading
the whole log. Export CSV fetches the full log newest-first in one
request, independent of the on-screen sort.

The Time column reads the way people talk: recent shots show "Today" or
"Yesterday" with the clock time to the minute, and older ones count back as
"3 days ago", "2 weeks ago", or "5 months ago". Shots older than a year show
their date instead. The CSV export always keeps the exact date and time down
to the second, no matter how the table displays it.

History averages (duration, yield, error, flow) use only **auto** shots
with actual weight at least 1 g from the last 10 stored entries, even
when the list is sorted by rating or oldest-first.

Every available weight curve has a **Flow rate (g/s)** chart directly below it
on Home and in its Stats history card. Both charts share a time axis with
reference lines every 10 seconds. The Weight chart adds lines every 10 g, and
Flow rate adds them every 0.5 g/s. Each displayed range rounds up to the next
reference interval and each chart grows vertically when all required labels
would not fit at its normal compact height.

The Flow rate chart draws the measured rates as one continuous line per color
instead of separate blocks. Samples are saved every half-second, so each
measured rate sits at the middle of the half-second it describes, and
neighboring rates are joined with a light three-point average, so the curve
reads as a smooth flow profile while the first and last measured rates stay
exact. Color segments (aqua flow, orange Fast guard, blue Slow guard, gray
A→M) break only where the extraction actually changes or where a gap leaves
nothing to draw.

The cards call shot output **Yield** while chart, goal, scale, and cup labels
continue to use Weight where they describe weight itself. **Avg flow** remains
the final yield divided by the time after first drop. **Max flow** is the highest
non-negative local change between usable consecutive curve samples; Home shows
the peak observed so far during a live shot, and saved cards reproduce it from
the stored curve. Max flow always reports the exact measured rates, not the
softened line. Falling weight contributes 0 g/s, while missing samples and
an A→M scale-loss period leave gaps instead of inventing flow. Exact partial
guard intervals remain usable. To avoid noisy boundary spikes without leaving
holes, the short interval after the exact first-drop marker rises halfway toward
the next contiguous measured flow, while the final boundary interval continues
the preceding contiguous flow. An interval ending at an exact Fast/Slow guard
or A→M marker continues the preceding contiguous flow the same way. When no
neighboring interval is available, the only measured boundary rate remains
visible. Max flow is unavailable when the curve has no usable interval.

The controller stores only the weight curve. The Flow rate chart and Max flow
are derived locally, so viewing or reloading them does not create another
history record or flash write.

## Read a result

CSV keeps its column order, with the shot output columns named like the cards:
`yield_g` for the final yield (earlier `actual_g`), `yield_source` for where
that number came from (earlier `actual_weight_source`), and the curve columns
`yield_dt_s` and `yield_<n>s` (earlier `w_dt_s` and `w_<n>s`). Everything else
keeps its earlier name, followed by `bbw_algorithm`, `bbw_algorithm_version`,
`bbw_alpha`, `bbw_learning_applied`, `preset_id`, the derived column
`max_flow_g_s`, and the yield curve columns. The derived column
is empty when a record has no usable curve interval. The JSON names
are `bbwAlgorithm`, `bbwAlgorithmVersion`, `bbwAlpha`, and `bbwLearningApplied`;
the JSON field names do not change with the CSV rename. These fields are
exported data; the visible table/cards and averages do not
add algorithm or offset fields. Spreadsheets and scripts that match earlier
exports by column name need the renames above.

The yield curve columns follow `max_flow_g_s` so every exported row also
carries the weights captured during that shot. `yield_dt_s` (JSON `wDtS`) is the
seconds between saved samples — 0.5 with current firmware — then one
`yield_<n>s` column holds each sample in grams — the same series as the JSON
`wCg` array, which counts in centigrams. Each column is named for the moment
its sample closes, so with half-second sampling the columns run `yield_0.5s`,
`yield_1s`, `yield_1.5s`, and so on up to the longest curve in the export —
the same dating the Weight and Flow rate charts use.
Plot a row's yield cells against their column times in a spreadsheet to
redraw its curve aligned with the shot's events. Shots without a saved
curve, and samples beyond a shot's own curve length, leave those cells empty.

The flow rate columns follow the yield curve columns and hold the same
measured rates the Flow rate chart and Max flow derive from that curve: one
`flow_<n>s` column per curve sample, in grams per second with two decimals.
`flow_1s` is the rate measured over the half-second that closes with the
sample in `yield_1s`, `flow_1.5s` the next, and so on. `flow_0.5s` stays
empty unless the exact first-drop marker falls within the first half-second,
in which case it carries the chart's rate for that interval. A falling weight
reports 0, and cells stay empty where the chart draws no flow: before the
first drop, across a missing sample, and during an A→M scale-loss period.
When no event marker falls on or inside a sample interval, a flow cell is
simply the rise between its two neighboring yield cells divided by the sample
interval.

`preset_id` (JSON `presetId`) and JSON `presetName` are captured from the preset
used for that shot, not the currently selected recipe. The name is a snapshot,
not a lookup through the mutable preset list, so it survives switching, renaming,
or deleting the preset. Pre-V3 records have unknown identity (JSON 0, CSV empty),
and records migrated from history V1–V4 have no recoverable preset name. IDs are
local to the controller, use 1–255 and may be reused after allocation wraps or
settings are reset; use the captured name when comparing historical shots.

`offset_g` is the compensation captured at shot start, **before learning**,
in grams at 0.01 g storage resolution. Zero is valid. It is not the baseline,
next learned offset or measured post-shutdown water. `bbw_alpha` is also
captured at shot start: **Linear regression + offset correction** (`legacy`, v1)
uses 1.00; historical adaptive EWMA v1 may use 0.10, 0.30, 0.50 or 1.00.
EWMA v2 supports exact hundredths from 0.01 through 1.00, including custom
gains such as 0.37. It is not a gain selected after this shot's result.
Learning applied is `1`/`0` in CSV and true/false in JSON; a skipped shot still
retains its assigned gain. For example, appended CSV values can be
`linear_ewma,2,0.37,1` and later `linear_ewma,2,0.50,1` for the same preset.

History schema V6 uses 72-byte records; the log now holds 100 shots. Each
shot's curve is saved by schema V3 with twice the resolution — a sample every
half-second — and older curve stores are discarded rather than migrated. The
curve partition grew to hold the finer samples, so updating from firmware
with the earlier layout or one-second curves requires a one-time clean USB
installation and starts the log empty; export the CSV first if you want to
keep older shots. Records written by older schemas are simply absent rather
than relabeled. Select Linear
regression + offset correction in current firmware for like-for-like
algorithm comparison. Renaming the visible method does not rename API/CSV
identifiers.

Algorithm identity describes the shot's configured policy even when a guard
or manual action ends it; use `stop`, `shot_type` and `cut_type` to interpret
the outcome. EWMA time/safety-limit outcomes remain in eligible history with
learning-applied false; only a normal weight-target cut can train EWMA.
Compare datasets separately by preset, algorithm/profile and
actual gain, recording firmware identity, recipe changes and resets externally.
Export before the ring overwrites older shots.

Average flow uses final weight (including accepted post-drip) minus baseline,
divided by duration after first drop. It is not terminal flow at cutoff.
The Flow rate chart is different: it shows the non-negative local change between
adjacent curve samples and can rise or fall throughout the shot.
Error and average flow share final weight algebraically, so their correlation
does not prove a residual-flow mechanism. The curve's revised endpoint does
not reconstruct post-stop drip decay. See [BBW learning](brew-by-weight.md#cutoff-algorithms-and-learning).

| Stop detail | Meaning |
| --- | --- |
| `normal_target` | Normal weight endpoint, including offset/prediction |
| `extended_max_weight` / `extended_min_time` | Fast guard extended the shot |
| `slow_max_time` / `slow_min_weight` | Slow guard reached its recovery boundary |
| `auto_to_manual` | Scale-loss time guard |
| `activator` / `web_stop` | Physical / authenticated Web stop |
| `wall_limit` / `hard_limit` | Operational / hard time limit |
| `cup_removed` | Cup-removal protection requested stop |

For example, 39 g against a 36 g goal with a Fast stop detail is not the same
calibration problem as a normal target cut followed by excess drip. Check the
reason before resetting learned offset. A live 2 s cycle can appear on Home,
but once completed it is excluded from persistent history and does not replace
the idle last-good card. An unrated shot uses rating 0.

USB: `CLEAR_SHOTS` (see [USB serial CLI](../SERIAL_CLI.md)).

Related: [Brew by weight](brew-by-weight.md), [FAQ](../FAQ.md),
[Wi-Fi](../settings/wifi.md) (timezone for wall-clock labels).
