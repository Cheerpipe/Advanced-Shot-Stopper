# Home Assistant

The native **Open Brew by Weight** integration adds one controller device,
live shot state, the newest recorded shot, an active-preset selector, seven
Home Quick Settings switches, and a safe restart button. It
does not create YAML helpers, template entities, REST commands, or automations,
and it cannot start or stop the espresso machine.

## Requirements

- Home Assistant 2026.9 or newer.
- Firmware that advertises `webhook_v1`, `preset_select_v1`,
  `quick_settings_v1`, `restart_v1`, and `stored_shots_v1`.
- Home Assistant and the controller on the same trusted local network.
- A LAN-reachable Home Assistant Internal URL that starts with `http://`.

The controller API is intentionally open over local HTTP, like the Web UI. Do
not use this integration on an untrusted or shared network.

Security scanners may flag library versions in this integration's development
and test environment, which mirror the versions Home Assistant itself ships.
At runtime the integration uses only the libraries your Home Assistant
installation already provides, so such warnings require no action on your
part: the development environment adopts Home Assistant's updated library
versions as they ship.

## Install and connect

Install **Open Brew by Weight** from its dedicated HACS repository after a
release is published, then restart Home Assistant. For development, copy
`custom_components/open_brew_by_weight` from the integration source into the
same folder under your Home Assistant configuration directory.

On networks that pass multicast, the controller announces itself and appears
under **Discovered** in **Settings → Devices & services**; selecting it and
confirming is the only step needed. The discovered card and the device show
your controller's name with each word capitalized — by default
**Openbrewbyweight**, or the custom name you set under Admin → Network. Home Assistant
identifies the device by its stable controller identity, so rediscovery after
an address change keeps the same entry up to date. If discovery does not find
the controller (for example on guest or hotel networks that block multicast),
add it manually:

1. In Home Assistant, go to **Settings → Devices & services → Add integration**
   and select **Open Brew by Weight**.
2. Enter only the controller IP address or local host name. On networks with
   discovery, the controller's address is `<device-name>.local`, for example
   `openbrewbyweight.local` (see
   [Discovery by name](../settings/wifi.md#discovery-by-name)).
3. If another webhook currently owns the controller, explicitly approve its
   replacement.

Home Assistant creates a secret webhook, registers it locally, configures the
controller, proves the callback with a correlated test, reads the initial state,
and then adds entities. No API credential or pairing step is required.

## Device and entities

The integration creates one device. It is published by **Cheerpipe**, and the
device page also shows the controller's hardware identity — for example
`esp32-s3-relay-x1-speaker` — alongside the firmware version. Home Assistant
also links the controller's **WiFi and Bluetooth addresses** on the same page,
so you can match the device to what your router or Bluetooth scanner shows. The
device page
offers a **visit** link that opens the controller's Web UI directly at its
friendly name-address, for example `http://controller.local/`, so you do not
need to keep the IP address at hand. Two diagnostic sensors capture that
identity once at setup and never change until the integration is reloaded:

- **Controller**, the controller hardware profile (for example
  `esp32-s3-relay-x1-speaker`), with the chip architecture (`n16r8`) and
  firmware version as attributes.
- **Machine**, the espresso machine the controller was built for (for example
  `La Marzocco Linea Micra (la-marzocco-linea-micra)`).
- **Controller IP**, the controller's current network address. It starts at
  setup and updates on its own whenever the router assigns a new one, even
  while Home Assistant shows nothing else changing.

Its remaining entities are:

- **Shot state** (`idle` or `brewing`).
- **Last shot** sensors — duration, final weight, target weight, average flow,
  first-drop time, star rating, shot type, stop detail, and preset. These
  mirror the newest recorded shot on the controller's idle Home page.
- **Last activation** sensors — when the machine last did something, what it
  was (a shot, a rinse, power on, or other), and how long it lasted. These
  mirror the newest entry on the controller's History page.
- **Stats** sensors — average duration, average yield, average BBW error, average
  flow, shots per day, and the shot count behind them. The controller computes
  the summaries over its ten most recent recorded shots; BBW error uses only
  normal target cuts in that window.
- **Active preset**, a select populated from the controller's preset names.
- **Brew by weight**, **No-scale BBW**, **A-to-M time guard**, **Slow
  extraction guard**, **Fast extraction guard**, **Avoid accidental touch**,
  and **Cup protection** switches.
- **Restart Open Brew by Weight**, a configuration button.

Selecting a preset waits for the controller to persist it and then refreshes
the authoritative value. If the machine is busy or the write fails, Home
Assistant keeps the last confirmed option and shows an error.

The Brew by weight switch changes the current session without rewriting the
recipe. No-scale BBW is machine-level: turning it off selects `off`; turning it
on restores the last observed non-off mode or `warn_once`. The five guard
switches update only the active preset and preserve its other recipe and learned
values. All switches are unavailable during an active cycle, and the five guard
switches plus No-scale BBW are also unavailable while effective BBW is off.

The restart button uses the controller's existing queued restart. It may be
pressed during a shot, but restart waits for that shot to finish. It cannot
close the machine circuit and the controller never resumes a cycle after boot.

## Updates and availability

Home Assistant reads a complete REST snapshot before it adds any entities.
The controller's `lastShot` is authoritative: it is the newest recorded shot,
or null after the log is cleared or an erase-all installation. The Web UI's idle
**Current / Last Shot** card reads that same record. A short, weightless, or
2 g-or-less activation leaves both views unchanged. Deleting the newest history
row reveals the next eligible shot; clearing the log clears both views.
Home Assistant can also read the older `lastGoodShot` field when connected to
firmware that still exposes the previous snapshot version.

After setup, validated webhooks update live state immediately. A completed-cycle
webhook triggers a REST reconciliation so the Last shot sensors continue to
mirror the newest recorded shot, even when that cycle was too short or light
to qualify. There is no healthy-state or background
polling. Home Assistant also performs a single bounded REST reconciliation after
a confirmed command, a revision gap, or a `controller_started` hint. A missed
final best-effort webhook can therefore leave values stale until one of those
triggers occurs.

If the controller is offline during setup or reload, Home Assistant keeps the
entry unavailable and applies its normal bounded setup retry; entities are not
created from an incomplete snapshot. A runtime REST failure keeps the last
confirmed values internally, marks entities unavailable, and starts at most one
recovery sequence after 5, 10, 20, 40, and 60 seconds. It stops on the first
successful full refresh or after the final attempt. After exhaustion, reload
the entry, retry an action, or wait for another valid webhook. A silent power
loss cannot be detected immediately without polling or a heartbeat, so entities
may show their last-known state until an operation fails.

The webhook path and callback URL are excluded from diagnostics and entity
attributes.

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

1. Remove the old plug-and-play brew control package and receiving automation from your Home
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
| Controller not discovered | Confirm both devices are on the same network and that it passes multicast; add the integration manually with the IP address instead |
| Callback URL unavailable | Set a LAN-reachable HTTP Internal URL in Home Assistant network settings |
| Another webhook is configured | Confirm takeover only if Home Assistant should become the sole receiver |
| Entities are unavailable | Confirm the controller address and LAN reachability; reload after the finite recovery sequence if no new action or webhook arrives |
| Callback test fails | Ensure the controller can reach Home Assistant's Internal URL and port |
| Callback repair issue appears | Open Reconfigure and save the displayed values again |

Generic receivers should use the separate [webhook guide](webhooks.md). The
firmware protocol is documented in the [Integration API](../INTEGRATION_API.md).
