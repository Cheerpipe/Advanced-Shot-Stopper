# Integration API

This is the public LAN contract used by the Open Brew by Weight Home
Assistant integration. It is available in firmware `0.1.0` and later at
`http://<controller>/api/v1/integration`. Like the Web UI API, it is deliberately
unauthenticated and must be used only on a trusted local network. It observes
brewing and manages the single webhook destination, active preset, Home Quick
Settings, and a safe queued controller restart. It cannot start, stop, rinse,
or directly actuate the machine.

## Common rules

- Requests and responses use `application/json`; request bodies are limited to
  2,048 bytes and response collections to `MAX_SHOT_PRESETS` items.
- Success responses contain `apiVersion: 1`, except the integration snapshot,
  which uses `apiVersion: 2` and `minimumClientApiVersion: 2` for its unified
  `lastShot` contract. Errors use
  `{"error":"STABLE_CODE","message":"English diagnostic"}`.
- All routes are open on the trusted LAN and use no token, pairing window, or
  authorization header.
- Durable mutations return `202` and a `requestId`; this acknowledges only that
  the existing control queue accepted the operation. Poll the request-status
  route until it reports `PERSISTED`. A mutation is not successful before that
  state. Clients time out after 10 seconds and may retry idempotent operations.
- Restart is the exception: its accepted response is final because the
  controller intentionally becomes unreachable. Do not poll its request ID.
- Webhook, preset, and Quick Settings mutations retain the firmware's existing
  safe-state gate and are rejected while a shot cycle is active. Restart is
  accepted during a shot but the existing control owner defers it until idle.

## Status and error codes

| HTTP | Code | Meaning |
| --- | --- | --- |
| 400 | `INVALID_REQUEST` | Malformed JSON, field, identifier, or bound |
| 404 | `PRESET_NOT_FOUND` | Stable preset ID does not exist |
| 409 | `CONFIG_LOCKED_DURING_ACTIVE_CYCLE` | Existing firmware safety gate is closed |
| 409 | `STALE_REVISION` | Quick Settings changed since the caller's snapshot |
| 409 | `REQUEST_BUSY` | Another persistent request owns the queue/staging slot |
| 413 | `CONTENT_TOO_LARGE` | Body exceeds the fixed request buffer |
| 500 | `PERSISTENCE_FAILED` | The durable write failed |
| 503 | `CONTROL_QUEUE_FULL` | The bounded control queue could not accept the command |
| 503 | `WEBHOOK_UNAVAILABLE` | Test event could not be queued or delivered |

## Routes

### `GET /api/v1/integration`

Returns identity, compatibility, current shot state, the newest recorded shot,
preset revision, and the complete Quick Settings snapshot.

```json
{
  "apiVersion": 2,
  "minimumClientApiVersion": 2,
  "deviceId": "AA:BB:CC:DD:EE:FF",
  "wifiMac": "AA:BB:CC:DD:EE:FF",
  "bluetoothMac": "AA:BB:CC:DD:EE:10",
  "manufacturer": "Open Brew by Weight",
  "model": "Open Brew by Weight",
  "firmwareVersion": "0.1.0",
  "capabilities": ["webhook_v1", "preset_select_v1", "quick_settings_v1", "restart_v1", "stored_shots_v1"],
  "shotState": "idle",
  "bootId": 123,
  "activePresetId": 2,
  "presetRevision": 19,
  "quickSettings": {
    "revision": 19,
    "activePresetId": 2,
    "brewByWeight": true,
    "noScaleBbwMode": "warn_once",
    "autoToManualGuardEnabled": true,
    "slowExtractionGuardEnabled": true,
    "fastExtractionGuardEnabled": true,
    "avoidAccidentalTouchEnabled": true,
    "cupProtectionEnabled": true
  },
  "lastShot": null,
  "lastActivation": null,
  "stats": {"shotCount": 0, "totalDurationS": 0, "avgDurationS": null, "avgYieldG": null, "avgErrorPct": null, "avgFlowGps": null, "shotsPerDay": null, "durationsS": []}
}
```

`lastShot` is nullable and describes the newest qualifying record in shot
history: a confirmed non-rinse cycle longer than 12 seconds with a valid final
yield over 2 g. Other completed activations appear only in `lastActivation`.
When present, `lastShot` contains
`cycleId`, `uptimeMs`, `durationMs`, `targetWeightG`, `presetId`, `presetName`,
`shotType`, `stopDetail`, `savePending`, and the optional `firstDropMs`, `weightG`,
`averageFlowGps`, and `rating` fields defined by the webhook contract. When the
controller has a valid clock at shot completion, it also includes
`endedAtUnixSec` (UTC) and `endedAtLocalSec`; otherwise both are absent.
The local value uses the saved time zone's offset at shot completion and stays
fixed if the zone changes later. Consumers should use `endedAtUnixSec` for an
unambiguous instant.
`savePending` is true while the deferred flash write still needs confirmation;
if saving fails, the record remains in RAM for a retry. Firmware with the
earlier v1 snapshot instead had distinct `lastShot` and `lastGoodShot` fields.
Clients that require that older meaning should check `apiVersion` before using
the new field.

`lastActivation` mirrors the newest activation-history record (the Web UI
History page). It is null before the first activation after a full data reset
and otherwise carries `id`, `type` (`shot`, `rinse`, `other`, `power_on`, or
`no_scale_guard_aborted`), `durationS`, `hasWallTime`, `endedAtUnixSec`, and
`endedAtLocalSec`.

`stats` carries the rolling aggregate shown at the top of the Web
UI Stats page: `shotCount`, `totalDurationS`, `avgDurationS`, `avgYieldG`,
`avgErrorPct`, `avgFlowGps`, `shotsPerDay`, and `durationsS` for the duration
chart. The window is the newest ten eligible shot records, including manual and
timer-only endings. `avgErrorPct` is the mean absolute percentage miss of only
normal BBW target cuts in that window. Unavailable averages are `null`.
The controller derives these values from RAM; the same object appears in every
`GET /api/v1/stats` response header under `stats`, alongside `savePending`.

`shotState` is `idle` or `brewing`. `wifiMac` and `bluetoothMac` repeat the
station interface addresses as upper-case `AA:BB:CC:DD:EE:FF` strings; the
Bluetooth address uses the controller's Bluetooth MAC base. Receivers may
surface them as device connections but must keep using `deviceId` as the
stable identity.

### `GET /api/v1/integration/request`

Returns the most recent durable command as
`{"apiVersion":1,"requestId":17,"state":"PERSISTED"}`. Compare its ID with
the mutation response. Valid terminal states are `PERSISTED`, `FAILED`, and
`CANCELED`.

### `GET /api/v1/integration/webhook`

Returns `enabled`, `url`, `brewState`, `firstDrop`, `end`, `presetChanges`, and
`deferDuringShot`. When that flag is true, queued requests wait while a shot is
active; a request that already started is never cancelled.

### `PUT /api/v1/integration/webhook`

Requires all webhook fields. The URL must be a bounded `http://` URL without
embedded credentials. Repeating the same request is successful and does not
create another destination.

### `POST /api/v1/integration/webhook/test`

Accepts `{"correlationId":"<1-64 URL-safe>"}` and queues the matching `test`
event. The event echoes `correlationId` so Home Assistant can prove that its
newly registered receiver is reachable.

### `GET /api/v1/integration/presets`

Returns a bounded snapshot:

```json
{"apiVersion":1,"activeId":2,"revision":19,"items":[
  {"id":2,"name":"Double","isFactory":true},
  {"id":1,"name":"Single","isFactory":true}
]}
```

Preset IDs are stable while an item exists and names are unique.

### `PUT /api/v1/integration/presets/active`

Requires `{"id":2}`. The `202` response identifies the existing persistent
command. After it reaches `PERSISTED`, read the preset snapshot for the
authoritative active value. A failed or timed-out write never reports the
candidate as confirmed.

### `PUT /api/v1/integration/quick-settings`

Requires `baseRevision` plus exactly one setting. Boolean settings are
`brewByWeight`, `autoToManualGuardEnabled`, `slowExtractionGuardEnabled`,
`fastExtractionGuardEnabled`, `avoidAccidentalTouchEnabled`, and
`cupProtectionEnabled`; `noScaleBbwMode` accepts `off`, `warn_once`, or
`require_scale`. Extra, duplicate, or incorrectly typed fields are rejected.

`brewByWeight` changes the current session override, `noScaleBbwMode` changes
the shared machine policy, and the five guard values change only the active
preset. The durable confirmation and authoritative-refresh rules are identical
to active-preset selection.

### `POST /api/v1/integration/restart`

Requires the exact empty object `{}`. A `202` response means the existing safe
restart path accepted the request. During an extraction it waits for the cycle
to end; it never closes the relay or resumes a cycle after boot. This trusted-
LAN route exposes no machine actuation counterpart.

### `POST /api/v1/diagnostic/scale-profile`

Starts, stops, or deletes the manual scale profile capture. Requires the
exclusive Web UI claim like every owned API route. The body is
`{"action":"start"}` or `{"action":"stop"}` or `{"action":"delete"}`; replies
`202 Accepted` with a request id that follows the common acknowledgement
rules. Bounded state (including `state`, `persistence`, counts, `stopReason`,
`lostCount`, and `canStart`/`canStop`/`canDelete`/`canDownload`) is reported
as `scaleProfile` in the diagnostic status document.

### `GET /api/v1/diagnostic/scale-profile/download`

Streams the most recent completed capture as a chunked `text/plain`
attachment (`Content-Disposition: attachment`,
`Cache-Control: no-store`). Disabled while a capture is recording; a trace
that stopped but has not finished saving downloads from memory and is labeled
accordingly. Concurrent Start/Delete are refused while the download streams.

## Web UI record API

The Stats page uses `GET /api/v1/stats`; History uses
`GET /api/v1/history`. These Web UI routes require the active
`X-WebUI-Client` obtained from `POST /api/v1/ui/claim`. They are separate from
the public integration routes described above.

Both responses include `bootId`, `total`, `offset`, `limit`, `hasMore`, and a
small `ui` object. Stats also returns `shots`, `stats`, and `savePending`;
History returns `history`. Paging and sorting parameters are unchanged:
`offset`, `limit`, and `dir` (`asc` or `desc`), plus `sort` (`date` or `rating`)
for Stats. The page sizes used by the browser are ten shots and twenty
activations.

`ui` contains `firmwareVersion`, `configMutable`, `webUiOverrideActive`,
`compatibilityMode`, `development`, `machineType`, `machineIntegration`,
`timeUtcSec`, and `lastCommand` (`requestId`, `state`). Its `config` contains
`revision`, `timezoneId`, `appliedTimezoneOffsetMinutes`,
`timezoneAutomatic`, and `timezoneInitialized`. These fields update shared
UI behavior, relative dates, and automatic timezone synchronization without
fetching Home's scale, cup, preset, or extraction state. The browser validates
and applies this state before displaying the records. Record requests also
retain the confirmation of pending Wi-Fi settings on a station connection.

The former `/api/v1/shots` route family has been renamed to `/api/v1/stats`:
external Web UI API callers must update the read URL and the POST URLs
`/api/v1/stats/clear`, `/api/v1/stats/delete`, and `/api/v1/stats/rate`.
The route rename preserves request bodies and mutation safety checks.
The public `/api/v1/integration` endpoints are unchanged.

Stats shot records and Home's `shotCurve` now carry paired `wCg` and `wAtMs`
arrays: accepted centigram weights and their elapsed reception times in integer
milliseconds, with up to 1201 observations. Use those times instead of the
removed shared `wDtS` interval. `wBreakBefore` lists zero-based indices that
start new continuous segments; `wTruncated` identifies an incomplete capture.
Curve event fields such as `dropS` and `endS` remain seconds and retain three
decimal places. Events are annotations, not extra received observations.
The [shot-history export contract](features/shot-history.md#read-a-result)
describes the corresponding ordinal CSV sample groups.

## Linea Micra Web API

Micra-compiled firmware exposes `POST /api/v1/machine/linea-micra` to the
claimed Web UI. It accepts one strict `action`: `connect`, `select`, `save`,
`refresh`, or `disconnect`. `connect` requires only `username` and `password`
and is rejected unless STA is connected and the setup AP is closed. It queues
cloud authentication and account-machine discovery but does not persist or
enable an account. `select` requires a `serial` from the latest discovery plus
the independent `applyTemperature`, `observeState`, and
`recognizeWakeGesture` booleans; it persists the
account credentials, installation key, and chosen machine through the normal
single-writer command path. `select` and `save` also carry the `powerOnWithScale`
and `shutdownWithScale` machine-link booleans with `shutdownGraceSeconds`,
and the `scaleOffWithMachine` scale-link boolean. `save` updates those options
for an already selected machine. `refresh` and `disconnect` accept no additional fields;
disconnect erases durable credentials/selection and the RAM session. Unknown,
duplicate, or action-inappropriate fields are rejected. Accepted commands
return `202`.

Settings and diagnostic status include a Micra subtree only in Micra firmware.
It reports whether an account is configured, selected name/serial, option flags,
request phase/error, ON/OFF/UNKNOWN power state, raw observed mode, evidence
quality, optimistic-ON and optimistic-OFF provenance, sample age/freshness,
selected target
temperature, STA/AP/shot gates,
last HTTP/transport status, and the bounded machine list returned while
connecting. Email, password, installation private key, access token, and refresh
token are never returned. `refresh` is read-only and requires a selected
machine; none of these fields authorizes machine actuation.

## Webhook version 1

All messages are POSTed as bounded JSON. Common fields are `schemaVersion: 1`,
`event`, `deviceId`, `bootId`, `cycleId`, `uptimeMs`, `timestamp`, and
`sentAtUptimeMs`. `brew_state`, `first_drop`, and `end` contain both `presetId`
and the name captured at cycle start in `presetName`.

`presets_changed` is emitted only after confirmed persistent preset mutation
and contains `activeId`, monotonic `revision`, and the same bounded `items`
array as the presets route. It is disabled for migrated webhook configurations
and enabled explicitly by the native integration.

`quick_settings_changed` is likewise emitted only after persistence. Its complete
Quick Settings snapshot is flat in the event envelope:

```json
{
  "schemaVersion": 1,
  "event": "quick_settings_changed",
  "deviceId": "AA:BB:CC:DD:EE:FF",
  "bootId": 123,
  "cycleId": 0,
  "uptimeMs": 930100,
  "timestamp": 1767225619,
  "sentAtUptimeMs": 930110,
  "revision": 20,
  "activePresetId": 2,
  "brewByWeight": true,
  "noScaleBbwMode": "warn_once",
  "autoToManualGuardEnabled": true,
  "slowExtractionGuardEnabled": true,
  "fastExtractionGuardEnabled": false,
  "avoidAccidentalTouchEnabled": true,
  "cupProtectionEnabled": true
}
```

`controller_started` is one best-effort boot hint containing controller identity,
`bootId`, and the current revision; receivers use it to perform one full REST
reconciliation. The existing
`presetChanges` subscription bit gates all integration-state events:
`presets_changed`, `quick_settings_changed`, `controller_started`, and
`integration_history_end`.

`integration_history_end` mirrors the newest activation-history record
(shot, rinse, other, power on, or a no-scale guard abort) and carries `id`,
`type`, `durationS`, `hasWallTime`, `endedAtUnixSec`, and `endedAtLocalSec` —
the same shape as the snapshot's `lastActivation`. It is emitted only after
the activation record is confirmed in history, including after controller
restart when the persisted `presetChanges` subscription remains enabled. A
receiver that misses it can
recover the value at the next reconciliation.

Receivers deduplicate by `(deviceId, bootId, cycleId, event, uptimeMs)`, reject
older events for the same boot, and refresh the REST snapshot when configuration
or preset revisions skip. Numeric shot fields must be finite and non-negative. Supported
shot types are `auto`, `timer_only`, and `manual`; supported stop details are
`normal_target`, `extended_max_weight`, `extended_min_time`, `auto_to_manual`,
`slow_max_time`, `slow_min_weight`, `cup_removed`, `activator`, `web_stop`,
`web_heartbeat`, `physical_override`, `hard_limit`, `wall_limit`,
`relay_safety`, `weight_anomaly`, `touch_weight_fallback`, and `other`. `prediction` is accepted as a
legacy alias for `other`.
`touch_weight_fallback` identifies a stop after sustained above-threshold
readings while accidental-touch protection blocks the normal cut. REST `lastShot`,
end webhooks and local history preserve this same value. Update strict receivers,
including the Home Assistant integration, before installing firmware that emits
it; older integration versions reject unknown stop details.

The webhook ID protects only the Home Assistant receiving endpoint; it does not
authenticate controller API calls. Do not log or include it in diagnostics.
Removing a Home Assistant entry conditionally clears only a callback whose
exact URL still belongs to that entry.
