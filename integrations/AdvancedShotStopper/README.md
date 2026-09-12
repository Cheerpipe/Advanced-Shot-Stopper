# Advanced Shot Stopper for Home Assistant

This HACS custom integration connects Home Assistant 2026.9 or newer to one
Advanced Shot Stopper controller over the local network. It provides live shot
state, controller-restored shot measurements, durable preset selection, seven
Home Quick Settings switches, and a safe restart button. It never starts or
stops the espresso machine.

## Install

After the dedicated integration repository publishes a release, add that
repository to HACS as an **Integration**, install **Advanced Shot Stopper**, and
restart Home Assistant. During development, copy
`custom_components/advanced_shot_stopper` into Home Assistant's
`custom_components` directory.

The controller must run firmware with integration API version 1. Set Home
Assistant's Internal URL to a LAN-reachable `http://` address. Both devices must
be on the same trusted network.

## Configure

1. Add **Advanced Shot Stopper** from **Settings → Devices & services**.
2. Enter only the controller IP address or local host name.
3. Confirm webhook takeover only if Home Assistant should replace the existing
   receiver.

Setup registers and tests the local callback before entities appear. The
controller API is intentionally open on the trusted LAN, like its Web UI. The
webhook ID is not included in entities or diagnostics.

## What it provides

- One controller device and 17 translated sensors.
- Last completed and last qualifying-good shot values: duration, final and
  target weight, average flow, first drop, type, stop detail, and preset name.
- A non-optimistic **Active preset** select backed by stable preset IDs.
- Seven non-optimistic Quick Settings switches and a shot-safe **Restart Shot
  Stopper** button.
- Immediate webhook updates with bounded reconciliation after commands,
  revision gaps, controller startup, or runtime failure; no periodic polling.
- Transactional address/webhook reconfiguration, redacted diagnostics, clean
  unload, and conditional removal cleanup.

A “good” shot retains the former YAML example's continuity rule: over 12
seconds and over 2 grams. It is not a quality rating.

## Limitations and common errors

- The controller supports one HTTP callback and no HTTPS callback.
- The controller API has no authentication and relies on the trusted LAN.
- Webhook delivery is best effort; a missed final event may remain stale until
  the next command, revision gap, controller-start hint, or reload.
- Preset and Quick Settings changes are rejected while the controller's
  existing safe-state gate is closed. Restart may be requested during a shot
  but waits until the controller is idle.
- A silent power loss cannot be detected before a REST operation fails because
  the integration deliberately has no polling heartbeat.
- No discovery is advertised, so setup requires the IP/host.

Use **Reconfigure** to view or rotate the administrator-only webhook ID, change
the address, or resend/test the current callback. Deleting the entry clears
only a callback it still owns.

Full setup, migration, entity, update, and troubleshooting details are in the
[Home Assistant guide](../../docs/features/home-assistant.md). The wire contract
is in the [Integration API](../../docs/INTEGRATION_API.md).

## License

Copyright 2024–2026 Felipe Urzúa and contributors. This integration is licensed
under AGPL-3.0-or-later with the rest of Advanced Shot Stopper.
