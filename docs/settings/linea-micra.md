# La Marzocco Linea Micra

Firmware built for the **La Marzocco Linea Micra** shows its controls in
**Settings → Machine and scale → La Marzocco Linea Micra**. Other machine
profiles omit the panel and its diagnostics.

The integration reads the selected machine through La Marzocco's cloud
service. It does not use the Micra's Bluetooth connection, so scale discovery,
scale commands, and weight streaming keep exclusive use of the firmware's BLE
client.

## Connect an account and select a machine

Shot Stopper must be connected to your normal Wi-Fi network in station mode.
Cloud communication is disabled while the setup access point is open or the
internet connection is unavailable.

1. Enter the email address and password used by the La Marzocco app.
2. Choose **Connect** and wait for the account's Linea Micra machines to appear.
3. Select the machine used with this Shot Stopper.
4. Choose **Use selected machine**. Only then can the three Micra options be
   edited and saved.

All three options start on. Before a machine is selected they remain visibly
checked but disabled, so the defaults are clear without implying that the
integration is already active.

After a machine is selected, Settings hides the account email, password,
**Connect**, machine list, and **Use selected machine** controls. **Selected
machine** shows the saved cloud account email, followed by the machine name and
serial number. Choose **Disconnect** to remove the saved credentials and
selected machine and make the connection controls available again.

With a machine selected, **Save Micra settings** saves **Allow brew boiler
temperature in presets**, **Monitor machine power state**, and **Recognize
paddle wake gestures**. It does not sign in again, validate the cloud account,
or reload the machine list.

If the account returns no Linea Micra machines, the account is not enabled and
the machine-specific options remain unavailable. Accounts with several Micras
show each returned name and serial number so the intended machine can be
selected explicitly.

The email, password, selected machine, and device installation key are stored
in the controller so it can sign in again after a restart. Settings returns the
saved email so it can display the read-only value; the password, installation
key, and short-lived access and refresh tokens are never returned by the Web UI
or diagnostics. The access token is reused for state reads and renewed after 50
minutes. Renewal normally replaces one scheduled state read, and the next read
occurs at the normal interval. After a restart, the first successful STA session
continues directly to a dashboard read so the displayed state is initialized.
It also reuses the secure HTTP connection while the cloud service allows it,
avoiding a new connection handshake for every read. **Disconnect**
removes the saved account credentials, installation key, selected machine,
cached list, in-memory session tokens, and cloud connection.

Scheduled reads and cloud failures do not disable **Disconnect** or the three
Micra options. Disconnect only forgets the saved integration; it does not send
a live command to the machine.

The cloud interface used by the La Marzocco app is not a documented public API
and can change independently of this firmware. A cloud outage or API change
affects Micra monitoring, but never scale operation, paddle handling, relay
safety, or local shot control.

## Monitor machine power state

Enable **Monitor machine power state** and save. Shot Stopper then queues a
dashboard read approximately every 30 seconds while STA is connected. It never
starts an automatic or requested read before STA connects, while setup AP mode
is open, or before the clock is synchronized. After startup, the first worker
opportunity following those conditions starts the initial dashboard read. A shot
or rinse keeps automatic and requested reads pending until local activity ends.

Diagnostics → Machine shows:

| Micra response | Displayed state |
| --- | --- |
| `StandBy` | OFF |
| `BrewingMode` | ON |
| Other valid mode or communication failure | UNKNOWN |

A confirmed sample is current for 30 seconds. After that, diagnostics retain its
last completed ON or OFF classification and mark the observation quality as
stale. A queued, running, paused, or retrying read also leaves that classification
unchanged. Failures use bounded 3, 6, and 9 second retry delays; only after all
four attempts fail does the state become UNKNOWN with communication-error
quality. The next automatic cycle then follows the normal 30-second cadence.
Select **(Refresh)** beside the displayed state to add a read to the same bounded
queue. It cannot bypass STA, AP, clock, shot, busy, or post-wake timing rules.

UNKNOWN is treated like ON for paddle behavior. It never qualifies a wake
gesture, so brewing and rinse behavior remain unchanged when a current OFF
observation is unavailable.

## Recognize paddle wake gestures

Keep **Recognize paddle wake gestures** on to use a fresh monitored OFF state.
The next physical paddle ON is then treated only as the Micra's standby wake
gesture. Shot Stopper mirrors the paddle through its normal relay safety path,
but it does not start brew or rinse, evaluate or consume guards, command the
scale, boost BLE discovery, play alerts, call brew webhooks, or add shot/rinse
history. Returning the paddle to OFF opens the relay and ends the gesture,
regardless of how long it was held. History adds one **Power ON** entry with
the gesture's date, time, and duration; Stats remains unchanged.

The OFF→ON edge immediately adds an optimistic ON overlay for at most 60
seconds—twice the normal read interval—and delays the next dashboard read for 15
seconds so the cloud can converge. The overlay makes the operational state ON
without rewriting the last cloud-confirmed OFF classification. **(Refresh)**
waits for the same deadline. The first successful read started after that delay
removes optimism and supplies the next confirmed classification. A request
started before the edge cannot publish stale OFF or change the deadline. A
failed read does not clear or extend optimism; after all retries, the confirmed
state becomes UNKNOWN. Paddle movement while the confirmed state is ON, UNKNOWN,
or stale OFF does not create or extend optimism and follows the normal brew/rinse
flow.

This option is independent from monitoring. Turning monitoring off keeps the
saved wake preference, but the effective state becomes UNKNOWN, so wake
recognition is inactive until monitoring produces a fresh OFF observation.

## Brew temperature in presets

**Allow brew boiler temperature in presets** reveals a per-preset value from
80.0 to 100.0 °C in 0.1 °C steps. New and factory-reset presets start at 93.0 °C;
duplicates copy the source value. Turning the option off or disconnecting the
account keeps every saved preset value.

When a preset change has been saved successfully, Shot Stopper sends its target
to the selected Micra. Saving an already-active preset also sends the target
after persistence when the temperature changed. A newly connected scale sends
the current active target again, which covers sessions where the machine or
controller was unavailable when the preset was selected. Repeated triggers are
combined, and only the latest active target remains pending.

Application waits while a shot or rinse is active and while the scale is
connecting. Either event cancels an in-progress cloud request without affecting
the relay, BLE connection, or local shot control; the latest target remains
pending and is retried after activity ends. Temporary cloud and authorization
failures use the same bounded retries and saved account session as power-state
monitoring. The integration never asks for credentials or registers a new
installation merely to retry a temperature.

The cloud command is complete only after a dashboard read reports the requested
target. Once the cloud accepts a change, delayed confirmation retries only the
dashboard read instead of resending the change. Status and diagnostics
distinguish a pending, running, confirmed, canceled, rejected, unconfirmed, or
communication-failed application from the Micra's last reported target, without
mixing that result into power-observation quality. Status also reports whether
the change was already accepted and whether another automatic attempt remains
scheduled.

Connection timeouts, rate limits, and temporary server failures remain pending
and retry after the cooldown. An expired session gets bounded refresh/sign-in
attempts first; if authorization remains invalid, automatic attempts stop. A
redirect or other permanent request rejection also stops automatic attempts for
that trigger, avoiding repeated cloud requests. Selecting or saving a preset
again, or reconnecting the scale, creates a new trigger. Turning this option
off, disconnecting the account, losing station Wi-Fi, or entering setup AP mode
prevents writes; saved preset values remain unchanged. Temperature application
does not require **Monitor machine power state** to be enabled.

Factory reset removes the Micra cloud account, selected machine, installation
key, and RAM session. The three Micra options return to their checked defaults,
and preset temperatures return to 93.0 °C. See
[Factory reset](factory-reset.md) and [Presets](../features/presets.md).
