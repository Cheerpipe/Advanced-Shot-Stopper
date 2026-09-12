# Home Assistant

The native **Advanced Shot Stopper** integration adds one controller device,
live shot state, the latest completed and qualifying shots, and an active-preset
selector. It does not create YAML helpers, template entities, REST commands, or
automations, and it cannot start or stop the espresso machine.

## Requirements

- Home Assistant 2026.9 or newer.
- Firmware that advertises integration API version 1.
- Home Assistant and the controller on the same trusted local network.
- A LAN-reachable Home Assistant Internal URL that starts with `http://`.

The controller API is intentionally open over local HTTP, like the Web UI. Do
not use this integration on an untrusted or shared network.

## Install and connect

Install **Advanced Shot Stopper** from its dedicated HACS repository after a
release is published, then restart Home Assistant. For development, copy
`custom_components/advanced_shot_stopper` from the integration source into the
same folder under your Home Assistant configuration directory.

1. In Home Assistant, go to **Settings → Devices & services → Add integration**
   and select **Advanced Shot Stopper**.
2. Enter only the controller IP address or local host name.
3. If another webhook currently owns the controller, explicitly approve its
   replacement.

Home Assistant creates a secret webhook, registers it locally, configures the
controller, proves the callback with a correlated test, reads the initial state,
and then adds entities. No API credential or pairing step is required.

## Device and entities

The integration creates one device. Its entities are:

- **Shot state** (`idle` or `brewing`).
- Duration, final weight, target weight, average flow, first-drop time, shot
  type, stop detail, and preset for the last completed shot.
- The same eight measurements for the last good shot. A good shot follows the
  legacy rule: duration over 12 seconds and final weight over 2 grams. This is
  a continuity filter, not a taste judgment.
- **Active preset**, a select populated from the controller's preset names.

Selecting a preset waits for the controller to persist it and then refreshes
the authoritative value. If the machine is busy or the write fails, Home
Assistant keeps the last confirmed option and shows an error.

## Updates and availability

Webhooks update entities immediately. Home Assistant also reads the controller
every five minutes, after preset changes, and when it detects a skipped preset
revision. This reconciliation repairs missed best-effort webhooks. An offline
controller makes the entities unavailable and Home Assistant retries without
requiring a restart.

The last completed shot and last good shot are kept in one local Home Assistant
store record, so they survive a restart. The webhook path and callback URL are
excluded from diagnostics and entity attributes.

## Reconfiguration

Use **Reconfigure** from the integration menu to:

- change the controller address;
- view or replace the webhook ID as an administrator;
- copy the resulting callback URL; or
- resend and test unchanged settings.

Treat the displayed webhook ID and URL as credentials. A replacement must be
32–128 URL-safe characters. Home Assistant registers the candidate first,
verifies it, adopts it, and only then removes the prior handler. If verification
fails it keeps the old entry and attempts to restore the previous controller
callback.

## Migrate from the YAML example

1. Remove the old Shot Stopper package and receiving automation from your Home
   Assistant configuration.
2. Remove its `input_number`, `input_text`, template sensors, and any obsolete
   `rest` or `rest_command` entries after deciding whether to retain their
   recorder history.
3. Restart Home Assistant and add the native integration using the steps above.
4. Approve callback takeover when asked.
5. Update dashboards and automations to use the new device entities. Entity IDs
   are assigned by Home Assistant and may differ from the old helper names.

Do not leave the old automation enabled: the controller has only one callback,
and two receivers cannot own it simultaneously.

## Removal and troubleshooting

Deleting the config entry disables the controller callback only when it still
matches this entry. A normal reload leaves the remote callback in place and
briefly unregisters only Home Assistant's local handler.

| Symptom | Resolution |
| --- | --- |
| Callback URL unavailable | Set a LAN-reachable HTTP Internal URL in Home Assistant network settings |
| Another webhook is configured | Confirm takeover only if Home Assistant should become the sole receiver |
| Entities are unavailable | Confirm the controller address and LAN reachability; reconciliation recovers automatically |
| Callback test fails | Ensure the controller can reach Home Assistant's Internal URL and port |
| Callback repair issue appears | Open Reconfigure and save the displayed values again |

Generic receivers should use the separate [webhook guide](webhooks.md). The
firmware protocol is documented in the [Integration API](../INTEGRATION_API.md).
