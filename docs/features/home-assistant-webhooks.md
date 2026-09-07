# Webhooks with Home Assistant

Send extraction start, first-drop, and final-result notifications to Home
Assistant. This integration observes shots; it cannot start the machine.

Use a trusted local network and Home Assistant's **HTTP** address, for example
`http://192.168.1.50:8123`. The firmware supports one destination, no HTTPS,
and no delivery retries. Forward events inside Home Assistant if you need
multiple consumers.

## Install the example

<a id="1-create-the-receiving-endpoint"></a>
<a id="2-receive-transform-and-save-each-notification"></a>
<a id="one-file-package-example"></a>
<a id="additional-entity-definitions"></a>

The complete [Home Assistant package](../examples/home-assistant-webhooks.yaml)
defines helpers, optional dashboard sensors, and one receiving automation.
Install it once; do not also create another automation with the same webhook ID.

1. Download the package into Home Assistant's configuration directory as
   `packages/shot_stopper.yaml`.
2. Enable packages in `configuration.yaml`. Merge into an existing
   `homeassistant:` section rather than creating a duplicate:

   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```

3. In the package, replace the example `webhook_id` with a long random secret.
   Keep `local_only: true` and POST as the allowed method.
4. Check the Home Assistant configuration, then restart it. Confirm that the
   automation **Shot Stopper — receive extraction** and the
   `input_number.shot_stopper_last_shot_duration` helper exist.
5. If you already use separate helper and automation files, merge the package's
   corresponding sections there instead. A standalone automation editor takes
   the single automation object, not the outer `automation:` list. Do not
   install both layouts.

The example uses current YAML automation/template syntax. Refer to the official
[packages guide](https://www.home-assistant.io/docs/configuration/packages/),
[webhook trigger](https://www.home-assistant.io/docs/automation/trigger/#webhook-trigger),
and [template sensors](https://www.home-assistant.io/integrations/template/).
Check compatibility with your installed Home Assistant version before use.

## Configure Shot Stopper and test

1. While idle, unlock **Admin → Webhooks**.
2. Enable webhooks and enter
   `http://<home-assistant-ip>:8123/api/webhook/<your-webhook-id>`.
   Use the same ID as the package and an address reachable from the controller.
3. Select brew-state, first-drop, and end events, then save.
4. Select **Send test**. Check Webhooks status and the Home Assistant
   automation trace. The test contains no measurements and deliberately does
   not update extraction sensors.
5. Complete a normal shot. After the drip delay, the raw duration/weight helpers
   should reflect its final result.

Reserve Home Assistant's address in your router if it would otherwise change.
Treat the webhook ID as a credential: someone who knows it can submit false data.

## Interpreting the saved values

- `last_shot` is the raw set, updated by received completed-shot events.
- `last_good_shot` updates only when duration is **over 12 s** and final weight
  is **over 2 g**. This is an example filter, not Shot Stopper's history policy
  or a taste-quality judgment.
- Missing optional fields leave the previous helper value unchanged. A displayed
  first-drop time or flow may therefore belong to an earlier shot; use the event
  payload if per-shot completeness is required.
- The `end` event is emitted after drip delay. Intermediate state events do not
  yet contain the settled result.
- Delivery is best effort. Missing messages cannot be used to prove the machine
  is idle or to implement safety control.

## If nothing arrives

| Check | Expected result |
| --- | --- |
| URL and secret | Local HTTP URL, correct port and exact matching webhook ID |
| Receiving automation | Enabled; one automation owns that webhook ID |
| Send test | Transport status in Shot Stopper; a trace in Home Assistant, but no sensor changes |
| Test arrives, values do not | Check event selections and helper entity IDs; inspect an `end` trace |
| Some measurements look old | That event may have omitted optional values; see the retention rule above |
| Network/server unavailable | No automatic delivery retry; correct the route and send a new test |

## Event payloads

Every message is a `POST` with `Content-Type: application/json`.

| Field | Description |
| --- | --- |
| `schemaVersion` | Format version, currently `1`. |
| `event` | `brew_state`, `first_drop`, `end`, or `test`. |
| `deviceId` | Wi-Fi MAC, such as `AA:BB:CC:DD:EE:FF`. |
| `cycleId` | Numeric extraction ID; use it to relate events. |
| `uptimeMs` | Event time in milliseconds since boot. |
| `timestamp` | Unix/UTC seconds; `0` means the clock is not synced. |
| `sentAtUptimeMs` | Time the message was prepared, in milliseconds since boot. |

### `brew_state`

Normal extraction sends `brewing` at the start and `idle` at the end.

```json
{"schemaVersion":1,"event":"brew_state","deviceId":"AA:BB:CC:DD:EE:FF","cycleId":42,"uptimeMs":912345,"timestamp":1767225600,"sentAtUptimeMs":912680,"state":"idle","durationMs":27800,"targetWeightG":36.0,"presetId":1,"stopDetail":"normal_target"}
```

| Field | Description | Domain |
| --- | --- | --- |
| `state` | Current state. | `brewing`, `idle` |
| `durationMs` | Duration in ms; `0` while brewing. | Number ≥ 0 |
| `targetWeightG` | Recipe target in grams. | Number ≥ 0 |
| `presetId` | Active recipe ID. | Integer ≥ 0 |
| `stopDetail` | End reason; empty while brewing. | See dictionary below |

### `first_drop`

```json
{"schemaVersion":1,"event":"first_drop","deviceId":"AA:BB:CC:DD:EE:FF","cycleId":42,"uptimeMs":895000,"timestamp":1767225583,"sentAtUptimeMs":895040,"firstDropMs":4480,"weightG":1.24,"targetWeightG":36.0,"presetId":1}
```

| Field | Description |
| --- | --- |
| `firstDropMs` | Time from start to first drops, in ms. |
| `weightG` | Scale reading at first drops, in grams. |
| `targetWeightG` / `presetId` | Active recipe target and ID. |

### `end`

```json
{"schemaVersion":1,"event":"end","deviceId":"AA:BB:CC:DD:EE:FF","cycleId":42,"uptimeMs":923400,"timestamp":1767225611,"sentAtUptimeMs":923480,"durationMs":27800,"targetWeightG":36.0,"presetId":1,"shotType":"auto","stopDetail":"normal_target","firstDropMs":4480,"weightG":36.72,"averageFlowGps":1.57}
```

| Field | Description | Domain |
| --- | --- | --- |
| `durationMs`, `targetWeightG`, `presetId` | Total time, target grams, and recipe ID. | Numbers ≥ 0 |
| `shotType` | How the shot was performed. | `auto`, `timer_only`, `manual` |
| `stopDetail` | Specific end reason. | See dictionary below |
| `firstDropMs`, `weightG`, `averageFlowGps` | First-drop time, final grams, and average g/s. | Optional decimal values |

### `test`

The **Send test** button sends the common fields with `event: "test"` and no
extraction measurements. It is useful for checking connectivity.

```json
{"schemaVersion":1,"event":"test","deviceId":"AA:BB:CC:DD:EE:FF","cycleId":0,"uptimeMs":930000,"timestamp":1767225618,"sentAtUptimeMs":930010}
```

## `stopDetail` dictionary

| Value | Meaning |
| --- | --- |
| `normal_target` | Reached the target weight normally. |
| `extended_max_weight` / `extended_min_time` | Fast-extraction guard extended weight or time. |
| `auto_to_manual` | A→M guard ended it after scale loss. |
| `slow_max_time` / `slow_min_weight` | Slow-extraction guard reached its time or weight boundary. |
| `cup_removed` | The cup was detected as removed. |
| `activator` | Configured physical activator ended it. |
| `web_stop` / `web_heartbeat` | Web stop requested, or web heartbeat timed out. |
| `physical_override` | Physical override was applied. |
| `hard_limit` / `wall_limit` | Firmware hard limit, or configured time limit. |
| `relay_safety` | Relay safety protection activated. |
| `weight_anomaly` | Weight anomaly detected. |
| `other` | No more specific reason. |
| `prediction` | Legacy value; not generated by new extractions. |
