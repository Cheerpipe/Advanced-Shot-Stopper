# Shot history

Automatic brew-by-weight shots with a connected scale are stored on the
controller and shown in the Web UI shot log. Use it to see why a shot ended
and how close it landed to the target.

## What is recorded

Only **automatic BBW** shots qualify: scale present at start, brew-by-weight
enabled (not timer-only), and automatic weight control active. Manual shots,
timer-only brews, and cycles without a scale are **not** stored.

Typical fields include local time (from the configured timezone offset),
duration, goal and actual weight, error, average flow, first-drop time,
whether Fast/Slow guards ran or extended the shot, `shot_type`, `cut_type`
(`auto`, `manual`, `limit`), `stop_detail` (for example
`normal_target`, `activator`, `web_stop`, `wall_limit`, `hard_limit`,
`extended_max_weight`, `cup_removed`), and a manual `rating` from 0
(unrated) to 5. Rate a finished shot from Home (Last/Current shot) or from
a history card by tapping a star; tapping the current star again clears the
score.

The log holds up to **120** shots. The following are never stored:

- Quick rinses and cycles shorter than 10 s
- Manual, timer-only, or no-scale shots
- Shots whose final weight is missing or below 1 g (e.g. scale off the
  machine or disconnected)

Those empty or sub-1 g shots are still shown on Home as the **last shot**
(status always reflects the current and last finished cycle). They are not
written to history, not used in averages, and not used for learned stop
offset or A→M samples.

Curve samples and the history record are written **once** when the cycle
closes (after the configured drip delay), not during an active brew. Shot
duration and the curve time axis end when the machine circuit opens. The
settled post-drip weight replaces the curve's endpoint at that same end time;
the drip-delay interval is not appended to the graph. The current-shot curve exposed to Home is an in-memory view; it is not a
persistent live-telemetry service.

## In the Web UI

Open the shot history table to browse rows, delete one entry, clear the
whole log, or export CSV. Clearing requires an explicit confirm.

Sort the list by **Date** or **Rating**, ascending or descending. Date
defaults to newest first. Rating puts unrated shots (0 stars) at the end
in both directions; equal scores keep newer shots first. The table loads
**10 shots at a time** as you scroll. The 20 s poll refreshes only the
first page of the current sort so new shots appear without re-downloading
the whole log. Export CSV fetches the full log newest-first in one
request, independent of the on-screen sort.

History averages (duration, weight, error, flow) use only **auto** shots
with actual weight at least 1 g from the last 10 stored entries, even
when the list is sorted by rating or oldest-first.

## Read a result

CSV retains its original column order and appends `bbw_algorithm`,
`bbw_algorithm_version`, `bbw_alpha`, `bbw_learning_applied`, and `preset_id`. The JSON names
are `bbwAlgorithm`, `bbwAlgorithmVersion`, `bbwAlpha`, and `bbwLearningApplied`.
These fields are exported data; the visible table/cards and averages do not
add algorithm or offset fields.

`preset_id` (JSON `presetId`) is captured from the preset used for that shot,
not the currently selected recipe. It survives switching, renaming or deleting
the preset. Pre-V3 records have unknown identity (JSON 0, CSV empty); their
preset cannot be recovered from target weight. Keep an external mapping from ID
to physical portafilter/basket and recipe context when exporting. IDs are local
to the controller, use 1–255 and may be reused after allocation wraps or settings
are reset; separate those epochs rather than pool unrelated physical setups.

`offset_g` is the compensation captured at shot start, **before learning**,
in grams at 0.01 g storage resolution. Zero is valid. It is not the baseline,
next learned offset or measured post-shutdown water. `bbw_alpha` is also
captured at shot start: Legacy v1 uses 1.00; adaptive EWMA v1 may use 0.10,
0.30, 0.50 or 1.00. It is not a gain selected after this shot's result.
Learning applied is `1`/`0` in CSV and true/false in JSON; a skipped shot still
retains its assigned gain. For example, appended CSV values can be
`linear_ewma,1,0.30,1` and later `linear_ewma,1,0.50,1` for the same preset.

Migrated pre-selector records keep their offsets, ratings and guard flags,
and identify Legacy with unknown version, gain and learning status (JSON null,
CSV empty). Unrecognized provenance is `unknown`, never the current setting.
History schema V3 retains 48-byte records and the 120-shot limit. V2 migration
preserves cutoff metadata and marks preset identity unknown. Older firmware
rejects V3; use the current firmware's Legacy selector for comparison.

Algorithm identity describes the shot's configured policy even when a guard
or manual action ends it; use `stop`, `shot_type` and `cut_type` to interpret
the outcome. EWMA time/safety-limit outcomes remain in eligible history with
learning-applied false; only a normal weight-target cut can train EWMA.
Compare datasets separately by preset, algorithm/profile and
actual gain, recording firmware identity, recipe changes and resets externally.
Export before the ring overwrites older shots.

Average flow uses final weight (including accepted post-drip) minus baseline,
divided by duration after first drop. It is not terminal flow at cutoff.
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
reason before resetting learned offset. A 2 s cycle shown on Home is excluded
from persistent history; an unrated shot uses rating 0.

USB: `CLEAR_SHOTS` (see [USB serial CLI](../SERIAL_CLI.md)).

Related: [Brew by weight](brew-by-weight.md), [FAQ](../FAQ.md),
[Wi-Fi](../settings/wifi.md) (timezone for wall-clock labels).
