# La Marzocco Linea Micra

Firmware built for the **La Marzocco Linea Micra** shows its controls in
**Settings → Machine and scale → La Marzocco Linea Micra**. Other machine
profiles omit the panel and its diagnostics.

The integration reads the selected machine through La Marzocco's cloud
service. It does not use the Micra's Bluetooth connection, so scale discovery,
scale commands, and weight streaming keep exclusive use of the firmware's BLE
client.

## Connect an account and select a machine

Open Brew by Weight must be connected to your normal Wi-Fi network in station mode.
Cloud communication is disabled while the setup access point is open or the
internet connection is unavailable.

1. Enter the email address and password used by the La Marzocco app.
2. Choose **Connect** and wait for the account's Linea Micra machines to appear.
   While the sign-in is checked with the La Marzocco cloud, the **Connect**
   button shows a small spinner and the status line follows each step of the
   sign-in; both settle as soon as the machines are listed or the sign-in
   fails, and a failed sign-in also names the reason.
3. Select the machine used with this controller.
4. Choose **Use selected machine**. Only then can the Micra options be
   edited and saved.

While the account is signed in but no machine is saved yet, **Selected
machine** reads **Signed in — select a machine** and the status line shows the
connection state, so you can tell a successful sign-in from one that has not
happened yet.

The connection type starts as **WebSocket**. The first three switches start on; the machine-aware scale search, scale power-on, scale shutdown, and
scale-off-with-machine options start off.
Before a machine is selected they remain visibly checked (or unchecked) but
disabled, so the defaults are clear without implying that the integration is
already active.

After a machine is selected, Settings hides the account email, password,
**Connect**, machine list, and **Use selected machine** controls. **Selected
machine** shows the saved cloud account email, followed by the machine name and
serial number. Choose **Disconnect** to remove the saved credentials and
selected machine, stop cloud checks, and make the connection controls available
again. The account status then reads **Unauthenticated**, and the Micra options
become unavailable until you connect and select a machine again. The connection
type preference is retained; a factory reset restores WebSocket.

With a machine selected, **Save Micra settings** saves **Connection type**, **Allow brew boiler
temperature in presets**, **Monitor machine power state**, the two
[Machine-aware scale search](#machine-aware-scale-search) options, **Recognize
paddle wake gestures**, **Turn machine on when the scale powers on**,
**Turn machine off when the scale powers off**, its **Machine shutdown delay**, and
**Turn scale off when the machine powers off**.
To switch between WebSocket and API, choose the connection type and save;
your connected account and selected machine are kept. Saving does not sign in
again, validate the cloud account, or reload the machine list.
The button is available whenever a machine is selected and settings are
editable, even when no option has changed.

If the account returns no Linea Micra machines, the account is not enabled and
the machine-specific options remain unavailable. Accounts with several Micras
show each returned name and serial number so the intended machine can be
selected explicitly.

The email, password, selected machine, and device installation key are stored
in the controller so it can sign in again after a restart. Settings and
Diagnostics show the saved email as a read-only value; the password, installation
key, and short-lived access and refresh tokens are never returned by the Web UI
or diagnostics. The access token is reused and renewed after 50
minutes. Authentication and machine commands use the cloud API with either
connection type. API observation reuses its secure HTTP connection when possible;
WebSocket observation renews its connection with the current token. **Disconnect**
removes the saved account credentials, installation key, selected machine,
cached list, in-memory session tokens, and cloud connection.

Scheduled reads and cloud failures do not disable **Disconnect** or the
Micra options. Disconnect only forgets the saved integration; it does not send
a live command to the machine.

The cloud interface used by the La Marzocco app is not a documented public API
and can change independently of this firmware. A cloud outage or API change
affects Micra monitoring, but never scale operation, paddle handling, relay
safety, or local shot control.

## Monitor machine power state

Enable **Monitor machine power state** and choose **Connection type**:

- **WebSocket** reads the current state through the API after each subscription
  starts, while receiving cloud updates in parallel. It is the default and
  avoids regular dashboard polling. A reconnect starts a new initial read;
  newer power updates take priority over that read.
- **API** checks the dashboard approximately every 30 seconds. Try it if a weak
  connection or the cloud service makes WebSocket unreliable.

Both wait for station Wi-Fi, a synchronized clock and a closed setup access
point. A shot pauses monitoring only while a scale is connected. Without a
connected scale, using the paddle keeps both WebSocket monitoring and API reads
available, whether you brew a shot, rinse or turn the machine on. If a shot with
a scale is classified as a quick rinse, the pause ends immediately; losing the
scale also allows monitoring to resume. Backflush gestures keep monitoring
available with or without a scale. WebSocket monitoring stays connected
during scale discovery, connection and reconnection. If monitoring needs a new
connection during scale setup or connection, it waits until that activity and
the scale's communication pause have ended. Wi-Fi and the local Web UI remain
available.
Micra API requests, including sign-in, Refresh and machine commands, wait
through scale connection. During a shot with a connected scale, reads wait.
Starting any shot cancels pending power commands, including shots without a
scale. Requests already in progress are canceled
by the cloud worker as soon as it observes the pause; local control never waits
for that cancellation.
Paddle wake gestures and starts blocked by a guard keep observation connected;
they do not count as shots or rinses.

Home shows **Machine power state** as **ON**, **OFF**, or **—** when unknown.
An active local estimate appears as **ON - Optimistic** or **OFF - Optimistic**.
If observation pauses during that estimate, its effective value stays on screen
with stale quality until a fresh power update arrives after reconnection.
Connection or temperature updates alone cannot confirm power.

Diagnostics → Machine distinguishes the last observed mode from its quality:

| Micra response | Displayed state |
| --- | --- |
| `StandBy` | OFF |
| `BrewingMode` | ON |
| `EcoMode`, missing or unsupported mode | UNKNOWN |

In WebSocket mode, the initial API reading stays current while the subscription
remains healthy, even if the machine sends no power update for several minutes
or hours. The next power update replaces it. Diagnostics shows the source beside
**Observed mode**, for example **StandBy (API)**, **StandBy (WebSocket)** or
**StandBy (API via WebSocket)**. The last label means the initial API reading
requested when the WebSocket subscription starts; a later manual API read shows
**API**. The sample age keeps increasing; unrelated messages do not reset it.
An explicit machine-offline report makes the retained value stale. If the
machine reports that it is back online without a power value, one new API read
resynchronizes it.

In API mode, power evidence becomes stale after 30 seconds. API failures use
four attempts with 3, 6 and 9 second delays. Regular API polling resumes its
30-second cadence after exhaustion and reports communication-error quality and
UNKNOWN unless a paused value is being retained. A failed WebSocket initialization does not
start recurring API polls or discard valid power updates already received.
WebSocket losses retain the last effective value as stale and retry with the
same delays, followed by a 60-second cooldown after exhaustion. Invalid
credentials require reconnecting the account or a manual refresh.
Pauses and network recovery preserve this authentication stop; they do not
restart automatic sign-in attempts.
**(Refresh)** adds a gated dashboard read. It follows the same shot, scale,
clock and post-wake rules; it does not enable continuous API polling in
WebSocket mode.

**Machine cleaning** is available through WebSocket only. It shows **Inactive**,
**Waiting for paddle**, **Cleaning in progress**, or an unknown reported value.
The row identifies its last update and marks retained values stale when the
stream is paused or the machine is offline. API mode shows that cleaning is
unavailable. A cloud cleaning update never starts the machine or turns an
existing shot into backflush. Transitions during a shot pause may be missed.

### Automatic backflush

With firmware enabled, WebSocket monitoring can supervise the automatic
backflush selected in the La Marzocco app. Follow the machine's cleaning
instructions and prepare the machine before requesting the program.

1. Leave the paddle OFF and request automatic backflush in the app.
2. Wait until Home reports **Backflush — waiting for paddle**, then move the
   paddle ON within the opportunity offered by the app.
3. Home first shows **Checking backflush**, then **Backflush in progress**
   when the machine confirms the program. Return the paddle OFF after it ends.

The controller does not reproduce the app's start countdown. Until the program
is confirmed, the usual 60-second protection applies. A qualified confirmation
allows at most **180 seconds from the original activation**, including the time
spent checking. The allowance ends as soon as the controller receives a state
outside backflush in progress. It also opens the activation circuit on loss of
supervision, a safety stop, or paddle OFF. The circuit opens at 180 seconds even
if the reported program is still running.

If the request ends before confirmation, the circuit opens and History records
**Other**. If the connection was just established, or a previous request was
interrupted, Home may ask you to cancel and request backflush again in the app.
That prevents an old request from receiving the longer allowance. An
unconfirmed or ambiguous activation never receives 180 seconds. When the
physical paddle remains ON after any stop, return it OFF before trying again;
restoring the connection cannot restart the machine.

Disabling observation, switching to API monitoring, or changing the connected
account or machine ends supervised backflush. Return the paddle OFF before
starting again. The previous cleaning request does not keep ordinary operation
blocked after that configuration change; the normal 60-second protection applies.

Backflush works without a scale. A connected scale keeps receiving weight, and
can reconnect during the program; backflush does not tare it, run its timer, or
add a shot to Stats. WebSocket monitoring continues throughout. Temperature
changes wait while cleaning is pending or active, and pending automatic machine
power commands are canceled. A start can be refused if a machine command is
still finishing or the cloud session needs renewal.

History records a confirmed activation as **Backflush**, including an
interrupted one. Its duration is the time the activation circuit was enabled;
the record does not certify a successful cleaning. Cloud updates can arrive
late, so opening follows the received state, manual stop or local safety limit,
not a guarantee of simultaneous physical completion. With firmware enabled,
API monitoring and other machine profiles retain their normal 60-second cap.
[Compatibility mode](firmware-mode.md) mirrors the physical paddle without a
duration cap and does not supervise automatic backflush.

### Cloud diagnostics and logs

**Diagnostic → La Marzocco Cloud**, immediately above **MISC**, shows the saved
account email and the active selected machine. The email is visible to anyone
who can view diagnostics. The panel also shows the most recently completed
cloud call: its start date and time in UTC, API name and HTTP method, result,
HTTP status when available, and duration in milliseconds. Duration helps spot
slow cloud connections. Before the first call, the panel reads **No calls yet**.
WebSocket connection state, retry time, message/power/pong ages, payload receive
and transmit rates, totals, errors and stop latency appear separately.
Two counters distinguish **Planned connections** from **Unexpected
reconnections**. Planned connections include the normal initial
connection and resumes after shots (including early rinse classification),
maintenance or firmware updates, account or monitoring changes, and routine
session renewal. Unexpected recoveries follow Wi-Fi loss, cloud or protocol
errors, or connection and message timeouts, even when the controller closes the
socket to recover. A recovery remains unexpected if a planned pause occurs
before reconnection. Only connections that reach the cloud subscription count;
failed attempts do not. Both counters reset when the controller restarts and
describe WebSocket monitoring, rather than individual HTTP API calls.
Payload byte counts exclude TLS and network overhead.
Connection setup allows up to ten seconds for each transport operation, so a
slow secure connection has time to complete. A connection error adds a warning
to the log with HTTP, TLS and socket error codes, without credentials or payloads.
The last completed HTTP call stays visible during the next request; this history is
kept until the controller restarts, including after Disconnect.

Each request logs its method and route when it starts, then its API name,
matching request ID, duration, HTTP status, and result when it finishes. These
entries use the existing serial and Web logs and respect their configured log
levels. Successful calls use **Info**; cancellations and temporary connection,
session, rate-limit, or server failures use **Warning**. Permanent rejections,
invalid or oversized responses, and request setup failures use **Error**.
Routes replace the machine serial with `{serial}`; logs never contain account
credentials, tokens, or response bodies. The transient `HTTP_CLIENT` header-wait
warning is hidden from serial output. Use the final result to tell whether the
call failed, even if the server returned HTTP 200.

UNKNOWN is treated like ON for paddle behavior. It never qualifies a wake
gesture, so brewing and rinse behavior remain unchanged when no confirmed ON
or OFF observation is available.

## Machine-aware scale search

Two checkboxes directly under **Monitor machine power state** tie scale
discovery to the machine's power state:

- **Boost scale detection when machine is on** — searches more actively for a
  scale while the machine is on, whatever the saved scan mode is.
- **Reduce scale scanning when machine is off** — uses less Bluetooth
  scanning time while the machine is off; finding a scale may take longer.

Both start off on new installs, after a factory reset, and after updating
from firmware that did not have them; a saved choice is kept across reboots
and updates until you change it. Each option acts only in its own state:
enabling the ON option changes nothing while the machine is off, and enabling
the OFF option changes nothing while it is on. When no matching state is
available — monitoring off, no account connected, the state unknown, or an
unsupported reading — the saved [BLE scan mode](scales.md)
applies unchanged.

The options need **Monitor machine power state** on, because they read the
same observed state. With monitoring off both checkboxes are disabled but
keep their saved choices. The options follow the same resolved state Home
shows, including optimistic ON/OFF estimates and values retained through a
temporary connection pause. There are no timers to configure or wait for,
and paddle or scale power events alone never change the search.

The override only changes how hard the controller searches for a scale; it
never stops discovery, powers a scale off, or changes the ESP32's CPU and
radio hardware policy (see [Power management](power-management.md)).

## Recognize paddle wake gestures

Keep **Recognize paddle wake gestures** on to use a monitored OFF state. The
last confirmed OFF counts while it is current or stale, so the next physical
paddle ON is then treated only as the Micra's standby wake
gesture. The controller mirrors the paddle through its normal relay safety path,
but it does not start brew or rinse, evaluate or consume guards, command the
scale, play alerts, call brew webhooks, or add shot/rinse
history. The gesture itself never changes scale discovery; the optimistic ON
it produces can select Aggressive searching only when
[Boost scale detection when machine is on](#machine-aware-scale-search) is
enabled. Returning the paddle to OFF opens the relay and ends the gesture,
regardless of how long it was held. History adds one **Power ON** entry with
the gesture's date, time, and duration; Stats remains unchanged.

The OFF→ON edge immediately adds an optimistic ON overlay for at most 60
seconds—twice the normal read interval—and delays the next dashboard read for 15
seconds so the cloud can converge. The overlay makes the operational state ON
without rewriting the last cloud-confirmed OFF classification. **(Refresh)**
waits for the same deadline. A fresh matching WebSocket power update can confirm the estimate earlier.
A contrary update retains it until the delayed API reconciliation or expiry.
The first successful reconciliation read after that delay supplies the next
confirmed classification. A request
started before the edge cannot publish stale OFF or change the deadline. A
failed read does not extend optimism. A pause retains the effective value until
a fresh power observation arrives, even after the estimate expires. Paddle movement while the confirmed state is ON,
UNKNOWN, or an optimistic overlay is already live does not create or extend
optimism and follows the normal brew/rinse
flow.

This option is independent from monitoring. Turning monitoring off keeps the
saved wake preference, but the effective state becomes UNKNOWN, so wake
recognition is inactive until monitoring produces a confirmed OFF observation.

## Turn machine on when the scale powers on

Keep **Turn machine on when the scale powers on** on to wake the Micra when
you switch the scale on. It is the mirror of the shutdown option above: the
machine turns on — the same wake the La Marzocco app performs — when the
scale comes back from its own power-off. A scale that merely reconnects after
a lost signal, and the first connection after the controller starts, never
wake the machine, so an ordinary reconnect never sends a cloud command.

The machine is never woken while a shot or rinse is running; that power-on
is ignored completely and nothing is queued for later. Starting a shot or rinse
also cancels a wake that was still waiting to run. Because the command
travels through the La Marzocco cloud, a saved, connected account is
required: without one the option stays disabled and has no effect even if it
was on before the account was removed.

The command uses the same retries as the shutdown option: if the controller
temporarily cannot reach the La Marzocco cloud when the scale powers on, the
command stays pending and is retried, so the machine wakes once the
connection returns. The moment the cloud accepts it, the controller treats
the machine as already on, exactly like the paddle wake gesture, and the
next fresh matching power update confirms that view; a delayed dashboard read
can reconcile a contrary update.
Turning this option off or disconnecting the account cancels a pending
wake.

Turning the scale off and then quickly back on combines naturally with the
shutdown option and its delay: switching the scale off starts the shutdown
countdown, and switching it back on inside the window cancels the shutdown
while leaving the machine on.

## Turn machine off when the scale powers off

Keep **Turn machine off when the scale powers off** on to put the Micra in
standby — the same state the La Marzocco app calls standby — when you switch
the scale off. It reacts only to the scale being switched off by its own
control: a scale that merely loses signal, stops reporting weight, or runs out
of range never turns the machine off.

The machine is never turned off while a shot or rinse is running. If the
scale is switched off during one, that power-off is ignored completely and
the shot finishes with its normal protections; nothing is queued for later.
Starting a shot or rinse also cancels a shutdown command still waiting to run.

The option only works with scales that report their power-off to the
controller. Because the command travels through the La Marzocco cloud, a
saved, connected account is required: without one the option stays disabled
and has no effect even if it was on before the account was removed.

When you enable the option, a **Machine shutdown delay** choice appears below it:
**OFF**, **5 s**, **15 s**, **30 s**, or **60 s**. With **OFF** the machine
goes to standby as soon as the scale powers off. With a delay, the controller
waits that long first, so switching the scale back on inside the window
cancels the shutdown and nothing happens. The delay also covers an accidental
power-off: pick the value that gives you comfortable time to notice and turn
the scale back on.

Turning this option off or disconnecting the account stops any pending
shutdown. If the controller temporarily cannot reach the La Marzocco cloud
when the delay ends, the command stays pending and is retried, so the machine
goes to standby once the connection returns (a shutdown queued while the clock
is still synchronizing is also sent as soon as readiness clears). The saved
delay is kept with the option so it is restored the next time it is enabled.

The moment the cloud accepts the shutdown command, the controller treats the
machine as already off: the operational state reads OFF for at most 60
seconds—twice the normal read interval—while the last confirmed reading still
says ON, and the next dashboard read is delayed 15 seconds so the cloud can
settle. This mirrors the wake gesture, where the paddle ON is trusted before
the cloud agrees. The first successful read started after that delay replaces
the overlay with the confirmed state. If no read succeeds within the 60
seconds, the confirmed ON simply becomes the visible state again until the
next read lands.

## Turn scale off when the machine powers off

Keep **Turn scale off when the machine powers off** on and the connected
scale switches itself off once the Micra is confirmed off — after the cloud
dashboard reports standby following a confirmed on state. A stale or
optimistic reading never triggers it, and it happens exactly once per
on-to-off transition, so a flaky connection cannot switch the scale off.

Only scales that accept a power-off command over Bluetooth are switched off.
See [Scales](scales.md#scale-power-off-support) for the explicit list. If the
connected scale does not support the command, nothing is written to it and
the log records a warning that the power-off was skipped; if no scale is
connected at that moment, nothing happens either.

The two directions never loop: this option runs only after the machine is
already confirmed off, so the disconnect that follows the scale powering
down cannot queue another machine shutdown, and the machine going to
standby through the shutdown option above only powers the scale off once.
Neither option acts while a shot or rinse is running.

After sending the scale shutdown command, the controller blocks any later
scale command on that connection. Its BLE library also blocks reads, scanning,
and reconnection for 3,000 ms, beginning before shutdown is submitted and
restarting the interval when the scale's disconnect is observed. Normal
reconnection resumes after that quiet period, without waiting for a second
advertisement.

Like every cloud option, it requires a saved, connected account: without one
the checkbox stays disabled. The account requirement covers the machine
observation; the scale command itself is local Bluetooth and works with the
connected scale regardless of brand.

## Brew temperature in presets

**Allow brew boiler temperature in presets** reveals a per-preset value from
80.0 to 100.0 °C in 0.1 °C steps. New and factory-reset presets start at 93.0 °C;
duplicates copy the source value. Turning the option off or disconnecting the
account keeps every saved preset value.

When a preset change has been saved successfully, Open Brew by Weight sends its target
to the selected Micra. Saving an already-active preset also sends the target
after persistence when the temperature changed. A newly connected scale sends
the current active target again, which covers sessions where the machine or
controller was unavailable when the preset was selected. Repeated triggers are
combined, and only the latest active target remains pending.

Application waits while a shot or backflush is active, while a backflush request
is pending, and while the scale is connecting. These conditions cancel an
in-progress cloud request without affecting
the relay, BLE connection, or local shot control; the latest target remains
pending and is retried after activity ends. If a change is queued while the
clock is still synchronizing or station Wi-Fi is not ready, it is sent as soon
as those conditions clear, without waiting for a cooldown. Temporary cloud and
authorization failures use the same bounded retries and saved account session
as power-state monitoring. The integration never asks for credentials or registers a new
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
key, and RAM session. The first three Micra options return to their checked
defaults, the machine-aware scale search, scale power-on and shutdown options
and the scale-off-with-machine option return to off, and preset temperatures
return to 93.0 °C. See
[Factory reset](factory-reset.md) and [Presets](../features/presets.md).
