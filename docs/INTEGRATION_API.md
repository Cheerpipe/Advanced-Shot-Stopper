# Integration API

This is the public LAN contract used by the Advanced Shot Stopper Home
Assistant integration. It is available in firmware `0.1.0` and later at
`http://<controller>/api/v1/integration`. Like the Web UI API, it is deliberately
unauthenticated and must be used only on a trusted local network. It observes
brewing and manages the single webhook destination and active preset; it cannot
start or stop the machine.

## Common rules

- Requests and responses use `application/json`; request bodies are limited to
  2,048 bytes and response collections to `MAX_SHOT_PRESETS` items.
- Every success response contains `apiVersion: 1`. Errors use
  `{"error":"STABLE_CODE","message":"English diagnostic"}`.
- All routes are open on the trusted LAN and use no token, pairing window, or
  authorization header.
- Durable mutations return `202` and a `requestId`; this acknowledges only that
  the existing control queue accepted the operation. Poll the request-status
  route until it reports `PERSISTED`. A mutation is not successful before that
  state. Clients time out after 10 seconds and may retry idempotent operations.
- Webhook and preset mutations retain the firmware's existing safe-state gate
  and are rejected while a shot cycle is active.

## Status and error codes

| HTTP | Code | Meaning |
| --- | --- | --- |
| 400 | `INVALID_REQUEST` | Malformed JSON, field, identifier, or bound |
| 404 | `PRESET_NOT_FOUND` | Stable preset ID does not exist |
| 409 | `CONFIG_LOCKED_DURING_ACTIVE_CYCLE` | Existing firmware safety gate is closed |
| 409 | `REQUEST_BUSY` | Another persistent request owns the queue/staging slot |
| 413 | `CONTENT_TOO_LARGE` | Body exceeds the fixed request buffer |
| 500 | `PERSISTENCE_FAILED` | The durable write failed |
| 503 | `WEBHOOK_UNAVAILABLE` | Test event could not be queued or delivered |

## Routes

### `GET /api/v1/integration`

Returns identity, compatibility, current shot state, the most recent completed
shot when available, and preset revision.

```json
{
  "apiVersion": 1,
  "minimumClientApiVersion": 1,
  "deviceId": "AA:BB:CC:DD:EE:FF",
  "manufacturer": "Advanced Shot Stopper",
  "model": "Advanced Shot Stopper",
  "firmwareVersion": "0.1.0",
  "capabilities": ["webhook_v1", "preset_select_v1"],
  "shotState": "idle",
  "bootId": 123,
  "activePresetId": 2,
  "presetRevision": 19,
  "lastShot": null
}
```

`lastShot`, when present, contains `cycleId`, `uptimeMs`, `durationMs`,
`targetWeightG`, `presetId`, `presetName`, `shotType`, `stopDetail`, and the
optional `firstDropMs`, `weightG`, and `averageFlowGps` fields defined by the
webhook contract. `shotState` is `idle` or `brewing`.

### `GET /api/v1/integration/request`

Returns the most recent durable command as
`{"apiVersion":1,"requestId":17,"state":"PERSISTED"}`. Compare its ID with
the mutation response. Valid terminal states are `PERSISTED`, `FAILED`, and
`CANCELED`.

### `GET /api/v1/integration/webhook`

Returns `enabled`, `url`, `brewState`, `firstDrop`, `end`, `presetChanges`, and
`deferDuringShot`.

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

## Webhook version 1

All messages are POSTed as bounded JSON. Common fields are `schemaVersion: 1`,
`event`, `deviceId`, `bootId`, `cycleId`, `uptimeMs`, `timestamp`, and
`sentAtUptimeMs`. `brew_state`, `first_drop`, and `end` contain both `presetId`
and the name captured at cycle start in `presetName`.

`presets_changed` is emitted only after confirmed persistent preset mutation
and contains `activeId`, monotonic `revision`, and the same bounded `items`
array as the presets route. It is disabled for migrated webhook configurations
and enabled explicitly by the native integration.

Receivers deduplicate by `(deviceId, bootId, cycleId, event, uptimeMs)`, reject
older events for the same boot, and refresh the REST snapshot when preset
revisions skip. Numeric shot fields must be finite and non-negative. Supported
shot types are `auto`, `timer_only`, and `manual`; supported stop details are
`normal_target`, `extended_max_weight`, `extended_min_time`, `auto_to_manual`,
`slow_max_time`, `slow_min_weight`, `cup_removed`, `activator`, `web_stop`,
`web_heartbeat`, `physical_override`, `hard_limit`, `wall_limit`,
`relay_safety`, `weight_anomaly`, and `other`. `prediction` is accepted as a
legacy alias for `other`.

The webhook ID protects only the Home Assistant receiving endpoint; it does not
authenticate controller API calls. Do not log or include it in diagnostics.
Removing a Home Assistant entry conditionally clears only a callback whose
exact URL still belongs to that entry.
