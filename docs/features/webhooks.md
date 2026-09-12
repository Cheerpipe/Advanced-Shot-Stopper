# Webhooks

Advanced Shot Stopper can send shot updates to one HTTP receiver on your local
network. Webhooks are useful for software other than Home Assistant; Home
Assistant users should follow the [native integration guide](home-assistant.md)
instead of configuring this page manually.

Webhooks observe extraction activity. They cannot start or stop the machine.
Delivery is best effort, uses plain HTTP, and has no retry queue, so it must not
be used as a safety signal. Keep the controller and receiver on a trusted LAN.

## Configure a receiver

While the machine is idle, open **Admin → Webhooks**, enter a reachable
`http://` URL, choose the events, and save. **Send test** delivers a message
without shot measurements. The status below the controls shows the latest
delivery result and drop count.

The controller supports one callback. Changing it replaces the previous
receiver. Enable **Queue webhooks during shots** only when the receiver's
network traffic disrupts a sensitive scale connection; queued delivery may be
delayed until the extraction ends.

For a quick local receiver, listen with a development HTTP tool and point the
controller to it. To inspect a captured payload with `curl`, save it as
`event.json` and run:

```sh
curl -X POST -H 'Content-Type: application/json' --data-binary @event.json \
  http://receiver.local:8080/shot
```

## Events

Every request is a bounded JSON `POST`. Common fields identify the schema,
event, controller, boot, cycle, event uptime, wall-clock time, and send time.
`timestamp` is `0` when the controller clock is not synchronized.

| Event | Purpose | Event-specific fields |
| --- | --- | --- |
| `brew_state` | Extraction entered `brewing` or returned to `idle` | `state`, duration, target, preset ID/name, stop detail |
| `first_drop` | First qualifying flow was confirmed | first-drop time, weight, target, preset ID/name |
| `end` | Settled final result after drip delay | duration, target, preset ID/name, type, stop detail, optional first drop/weight/flow |
| `presets_changed` | Persisted preset inventory changed | active ID, revision, full bounded item list |
| `quick_settings_changed` | Persisted Home Quick Settings changed | revision, active preset ID, mode, and all six boolean values |
| `controller_started` | Network-ready controller boot asks a client to reconcile | boot ID and current configuration revision |
| `test` | Receiver connectivity check | optional correlation ID used by API clients |

Preset names on shot events are captured when the cycle starts. Renaming a
preset later does not rewrite the name attached to that shot. The
`presetChanges` is the shared subscription for `presets_changed`,
`quick_settings_changed`, and `controller_started`. It is disabled for existing
configurations until selected by a compatible client. Settings events are sent
only after persistence succeeds; the startup hint is best effort and is not a
heartbeat.

Example completed shot:

```json
{"schemaVersion":1,"event":"end","deviceId":"AA:BB:CC:DD:EE:FF","bootId":123,"cycleId":42,"uptimeMs":923400,"timestamp":1767225611,"sentAtUptimeMs":923480,"durationMs":27800,"targetWeightG":36.0,"presetId":2,"presetName":"Double","shotType":"auto","stopDetail":"normal_target","firstDropMs":4480,"weightG":36.72,"averageFlowGps":1.57}
```

See the [Integration API](../INTEGRATION_API.md) for the complete field,
enumeration, security, and compatibility contract.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Test fails | Receiver address is HTTP, reachable from the controller, and listening on the configured port |
| Test works but shots do not arrive | Webhooks and the required event selections are enabled |
| Some measurements are absent | Optional measurements are omitted when the scale did not provide a valid value |
| Messages arrive late | Disable queuing unless your scale link needs it |
| A second receiver stops receiving | Only one callback can own the controller at a time |
