# Shot history

Completed shots with a measured yield are stored on the controller and shown
in the Web UI shot log. Use it to see how long a shot ran, why it ended, and
how close it landed to the target when brew by weight was in use.

When you open **Stats**, the Stats section, including its chart, and the
Shot history section, including its sort, export, and clear controls, stay
hidden behind loading animations until the shot data is ready.

## What is recorded

A confirmed, non-rinse cycle becomes a shot when it lasts **more than 12 seconds**
and its settled final yield is valid and **more than 2 g**. Manual stops,
timer-only brews, normal BBW cuts, guard stops, and time or safety limits all
use this same rule. A cycle without a valid measured yield cannot qualify.
The 12-second recording rule is fixed; changing BBW's start-of-shot protection
time changes weight stopping, not which completed cycles are recorded.

Typical fields include local time (from the saved time zone at shot end),
duration, the exact preset name captured at shot start, goal and yield (the
shot's actual output), error, average flow, first-drop time, the scale used
(your chosen name, otherwise the suggested model name, or the advertised
Bluetooth name when neither is available), the late-tare time when the cup was
tared mid-shot, whether Fast/Slow
guards ran or extended the shot, `shot_type`, `cut_type`
(`auto`, `manual`, `limit`), `stop_detail` (for example
`normal_target`, `activator`, `web_stop`, `wall_limit`, `hard_limit`,
`extended_max_weight`, `cup_removed`), and a manual `rating` from 0
(unrated) to 5. Rate a stored shot from its history card. The same stars are
available on Home's **Current / Last Shot** card while the shot is the newest
eligible history row; tapping the current star again clears the score.
Under the duration, that Home card also shows when the shot ended, such as
“Today at 19:06”, using the same friendly time wording as the Stats history
cards. The time appears only when the clock was set when the shot finished.
The preset snapshot is also shown on that Home card and on every Stats history
card. Renaming or deleting a preset later does not rewrite a shot's displayed
name.

**Scale** appears after **Preset** and keeps the name captured for that shot,
even if the scale disconnects or is renamed later. **Rate** aligns with the
middle column; long scale names wrap within the first column. Existing shots
keep their original names. A Bluetooth name longer
than the friendly-name limit is shortened to fit. **Tare time**, before
**1st drop**, shows the successful late tare's elapsed shot time, or **None**
when no late tare succeeded. Its chart label always uses one decimal, such
as **3.0 s**. The initial shot-start tare is not a late tare.

If a late retare succeeds and a fresh reading confirms its new zero, an earlier
first-drop time and marker are discarded. Later coffee can supply a new time
and marker; without that replacement, first-drop time and **Avg flow** are
unavailable. The rest of the shot curve is retained. **Tare time** records
successful command completion, so it can also appear when zero was not
confirmed and the earlier first-drop record was kept.

The log holds up to **100** shots. The following are never stored:

- Rinses and starts that were not confirmed
- Cycles lasting 12 seconds or less
- Cycles whose final yield is missing, invalid, or 2 g or less

Those cycles do not replace the idle Home shot, but confirmed activations still
appear in the separate [activation history](activation-history.md). During a
live cycle, Home shows the current cycle. BBW offset learning and A→M samples
keep their own eligibility rules.

The controller collects accepted scale readings in memory while the shot runs
and saves the completed curve after the configured drip delay. Each reading
keeps its actual arrival time, including repeated weights and changing scale
cadence. Duration ends when the machine circuit opens; later drip readings
are not added to this series. Settled post-drip yield remains a separate
endpoint annotation, so it cannot change an earlier scale reading or its flow.
Isolated readings and event weights appear as small dots at the curve's line
thickness, including when the chart is stretched across a wide screen.

A curve can hold 1201 readings: enough for a 60-second shot with readings
50 ms apart. If a faster burst exceeds that capacity, the captured prefix
remains available and the chart says **Incomplete curve**. Brew stopping and
the shot's summary still work. Known rejected readings, a new tare reference
and scale loss leave breaks in the curve. A healthy slow scale does not
create a break merely because its readings are farther apart.

The first-drop marker and late-tare cup marker keep their event times. Fast,
Slow and A-to-M colors identify changes without adding competing time labels;
the time axis uses 10-second references. Home shows the live memory curve
during a shot and the newest eligible history row's exact curve while idle.

Observed removal or a new placement during the drip delay preserves the weight
captured at shot end and discards post-drip learning, regardless of the idle
automatic-tare setting. A single fresh reading consistent with the known empty
pan is enough to discard this optional analysis, but cannot trigger a replacement
tare. If a physical swap is completely absent from the scale notifications,
weight alone cannot establish that the final reading belongs to another cup.

## In the Web UI

Open the shot history table to browse rows, delete one entry, clear the
whole log, or export CSV. Clearing requires an explicit confirm.

Home and Stats read the same shot history. Deleting the newest shot shows the
next newest qualifying shot on Home; clearing the log empties the idle card.
Factory reset clears both histories. The [activation history](activation-history.md)
records every confirmed activation, including rinses and cycles that do not
qualify as shots. Deleting or clearing shot history never changes that diary.
New rows appear after the configured drip delay. If flash saving is still
pending, the API reports `savePending`; a failed write remains pending for retry.
Once saving completes, the newest shot and its curve survive a restart.
Power loss before saving completes can still lose pending changes.

When you first open **Stats**, both **Stats** and **Shot history** show a loading
wave. They fade away together once the summaries and history are ready. The
wave over Stats does not add space to the panel or resize it when it disappears.

Sort the list by **Date** or **Rating**, ascending or descending. Date
defaults to newest first. Rating puts unrated shots (0 stars) at the end
in both directions; equal scores keep newer shots first. The table loads
**10 shots at a time** as you scroll. The 20 s poll refreshes only the
first page of the current sort so new shots appear without re-downloading
the whole log. Export CSV fetches the full log newest-first in one
request, independent of the on-screen sort.

The Time column reads the way people talk: "Today" or "Yesterday" with the
clock time to the minute, the weekday name for the rest of the week, then
"2 weeks ago" for older weeks, and a short date like "Sep 15" (with the year
when it is not the current one). Hovering a time shows its exact date and
time to the second, and the CSV export always keeps that full detail no
matter how the table displays it.

Stats uses up to the newest **10 qualifying shots** for duration, yield, available
flow, daily count, and the duration chart, regardless of how the table is
sorted. **Avg BBW error** independently uses up to the newest **10 normal BBW
target cuts** in the full history, skipping manual shots and Fast/Slow guard
stops. It averages the absolute percentage miss, so overshoots and undershoots
cannot cancel each other. It is unavailable when there is no qualifying BBW
target cut. For example, a single 28.0 s Fast-guard shot yielding 36.9 g with
1.42 g/s average flow shows those same three averages; **Avg BBW error** stays
unavailable until a normal BBW target cut is recorded. **Daily shots**
averages the dated shots over the calendar days they span, including both ends.
An erase-all firmware installation starts with empty shot history.
If a low-weight older row is imported later, it remains in the table and CSV
but does not enter Home or these summaries.

Every available weight curve has a **Flow rate (g/s)** chart directly below it
on Home and in its Stats history card. Both charts share a time axis with
reference lines every 10 seconds. The Weight chart adds lines every 10 g, and
Flow rate adds them every 0.5 g/s. Each displayed range rounds up to the next
reference interval and each chart grows vertically when all required labels
would not fit at its normal compact height.
On a narrow screen, a few numbers beside these charts may be hidden when they
would overlap. The lines and measurements stay in place, and more numbers
appear again when there is room. Home applies the same rule to the time and
weight references above the shot and to the Current / Last Shot weight bar.
That bar always pairs the measured weight with the target, shown as
`35.4 g / 36 g` where the shot actually landed, while the target alone still
marks its own position; when the two labels are too close, the paired
measured/target label is the one that stays visible.
The shot card continues to show the exact measured weight.

The Flow rate chart measures the weight change over the preceding second using
the readings' actual arrival times. If a second starts between two readings,
their weights supply an interpolated starting value. The whole window needs
continuous readings on or after first drop. Before enough readings arrive,
or after a known rejection, tare or scale-loss break, measured flow is unavailable.
The flat line before first drop is only a visual reference. With uninterrupted
readings, a short visual connection joins it to the first available flow point.
That connection is not a measured rate and does not contribute to Max flow or
the CSV export. Known interruptions remain gaps.

Each estimate is drawn at the middle of its measurement window, usually half a
second before the latest reading. With a healthy scale reporting less often
than once per second, flow describes the actual longer span between readings
and is drawn at its midpoint. A long interval alone cannot distinguish a slow
scale from an interruption that the controller could not detect. Repeated weights
are retained. When several readings share an arrival time, the last one supplies
the endpoint; earlier readings at that time have no separate flow value.
Event annotations and settled yield never enter the measured flow series.
Flow calculations preserve first-drop and cutoff timing to the millisecond,
so readings just before the cutoff remain available and a window cannot start
before first drop.

The cards call shot output **Yield** while chart, goal, scale, and cup labels
continue to use Weight where they describe weight itself. **Avg flow** remains
the final yield divided by the time after first drop. **Max flow** is the highest
non-negative estimate from these supported windows; Home
shows the peak so far during a live shot, and saved cards reproduce it from
the stored observations. Falling weight contributes 0 g/s. Max flow is
unavailable when the curve has no supported window. For an **Incomplete curve**,
Max flow is marked **(recorded)** and describes only the captured portion of the
shot. The flow line ends at the last supported window without extending to the
settled yield. No extra smoothing changes the plotted values.

The controller stores only the weight curve. The Flow rate chart and Max flow
are derived locally, so viewing or reloading them does not create another
history record or flash write.

## Read a result

The CSV exports `local_time` as the local date and time saved for that shot,
`ended_at_unix` as UTC Unix seconds, and `tz_off` as the signed offset in
minutes applied when it ended. For example, two shots in `America/Santiago`
may have different `tz_off` values in winter and summer. Changing the saved
zone later does not recalculate either row. If the clock was unavailable,
`has_wall_time` is `0` and all three time cells are empty. A zone name is not
recorded per shot.

CSV retains the scalar shot columns, including `yield_g`,
`yield_source`, `max_flow_g_s`, the BBW fields and `preset_id`.
After those columns, `curve_truncated` reports `1` for an incomplete
capture and `0` otherwise. `curve_break_before` lists the zero-based
indices of readings starting a new segment, separated by semicolons.

Each reading has an ordinal group: `sample_1_time_s`,
`sample_1_weight_g`, `sample_1_flow_g_s`, then the same three columns
for sample 2 and so on. Times are the original elapsed arrival times in
seconds, weights are grams, and flow is grams per second. A flow cell stays
empty when that reading has no supported flow window. The time is the window's
ending observation time, while the chart uses its midpoint; arrival times retain
millisecond precision (up to three decimal places in seconds). The chart,
Max flow and CSV use the same estimates. Missing curves
and groups beyond a shot's captured length leave empty cells. The export
does not resample to a grid, so different scale cadences remain visible without
creating a column for every distinct arrival time.

The underlying curve JSON pairs centigram weights in `wCg` with integer
elapsed milliseconds in `wAtMs`. `wBreakBefore` supplies the same
segment-start indices and `wTruncated` reports completeness.
Spreadsheets using the former `yield_dt_s`, `yield_<time>s` or
`flow_<time>s` columns must switch to these ordinal sample groups.

`preset_id` (JSON `presetId`) and JSON `presetName` are captured from the preset
used for that shot, not the currently selected recipe. The name is a snapshot,
not a lookup through the mutable preset list, so it survives switching, renaming,
or deleting the preset. Pre-V3 records have unknown identity (JSON 0, CSV empty),
IDs are
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

The one-time USB curve-layout update starts the old weight curves empty while
preserving settings, shot summaries and activation history. Subsequent firmware
updates can use OTA. See [Build](../BUILD.md#curve-layout-transition).
Select Linear
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
The Flow rate chart is different: it shows the non-negative weight change over
supported measurement windows and can rise or fall throughout the shot.
Error and average flow share final weight algebraically, so their correlation
does not prove a residual-flow mechanism. The curve's revised endpoint does
not reconstruct post-stop drip decay. See [BBW learning](brew-by-weight.md#cutoff-algorithms-and-learning).

| Stop detail | Meaning |
| --- | --- |
| `normal_target` | Normal weight endpoint, including offset/prediction |
| `touch_weight_fallback` | Touch protection blocked the weight stop and the sustained-weight backup ended the shot; Ended shows Touch fallback on Home and Stats, matching Home Assistant's Last shot stop detail |
| `extended_max_weight` / `extended_min_time` | Fast guard extended the shot |
| `slow_max_time` / `slow_min_weight` | Slow guard reached its recovery boundary |
| `auto_to_manual` | Scale-loss time guard |
| `activator` / `web_stop` | Physical / authenticated Web stop |
| `wall_limit` / `hard_limit` | Operational / hard time limit |
| `cup_removed` | Cup-removal protection requested stop |

For example, 39 g against a 36 g goal with a Fast stop detail is not the same
calibration problem as a normal target cut followed by excess drip. Check the
reason before resetting learned offset. A live 2 s cycle can appear on Home,
but once completed it is excluded from shot history and does not replace the
idle last-shot card. An unrated shot uses rating 0.

USB: `CLEAR_SHOTS` (see [USB serial CLI](../SERIAL_CLI.md)).

Related: [Brew by weight](brew-by-weight.md), [FAQ](../FAQ.md),
[Wi-Fi](../settings/wifi.md) (timezone for wall-clock labels).
