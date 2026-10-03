# First setup and daily use

Use this guide after firmware installation and the applicable
[bench checks](MANUAL_TEST_PLAN.md). For equipment and wiring prerequisites,
start with [Hardware](HARDWARE.md).

## Connect for the first time

1. Keep the physical activator OFF (release a momentary button) and power the
   controller. Turn on the scale with no other app connected to it.
2. Join the controller's [fallback access point](settings/ap.md#first-connection)
   from your phone or computer. The name is **`OpenBrewByWeightAP-`** plus
   eight characters unique to this controller (USB `AP_STATUS` shows the exact
   name). Stay connected even if the phone reports "no internet"; open the AP
   address in a browser. The scale may wait to connect until you finish this
   setup network.
3. If the Web UI shows **Reload**, select it to claim this browser session.
   Open **Admin** and unlock it with the device password.
4. Change the factory device password in **Admin → Device password**. It is
   shared by Admin and the access point. If the AP disconnects after a
   password change, reconnect using the new password.
5. In **Admin → Wi-Fi**, enter your home network and save. Rejoin that network
   on your phone/computer when the controller's AP closes.
6. Open the controller within the 3-minute confirmation window. On most home
   networks it answers at `openbrewbyweight.local`, its default device name
   (a renamed controller answers at `<name>.local` instead). If that name does
   not open, find the controller's IP address in the router's connected-device
   list or via [USB NET_STATUS](SERIAL_CLI.md). The old AP address does not
   automatically become a home-network address. A successful browser claim
   confirms the new settings; unreachable settings revert.

You can also operate locally over the AP while your client remains associated.
The [AP guide](settings/ap.md) explains its idle shutdown and recovery.

## Web access

**Claim / Reload** gives one browser control of Home and Settings. Another client
can take the claim, and 15 minutes without using a control locks the page and
stops polling. Scrolling does not keep it active; use Reload to resume.

The header shows the controller's home-network Wi-Fi signal and its Bluetooth
link to the scale. More lit arcs or bars mean a stronger signal; amber means
weak reception. Tap Wi-Fi to see the connected network name and signal level;
tap Bluetooth to see the connected scale and its signal level. The scale uses
your friendly name when set, or the name reported over Bluetooth. Tap the same
icon again, tap outside the popup, or press Escape to close it. A crossed-out
Bluetooth symbol means the scale is disconnected.
Dim icons without a cross
mean a reading is not available yet, or the page cannot refresh it. The Wi-Fi
icon describes the home-network connection, even when you open the page through
the controller's own access point.

On desktop, scrolling down Home moves the logo out of view and brings the menu
up beside the Wi-Fi and Bluetooth icons. This compact header stays visible as
you scroll. Return to the top to see the logo again. Phone layouts and other
pages keep their usual header.

**While a page loads**: Home, Settings, Stats, History, Diagnostics, and Admin
show a loading view with the Open Brew by Weight mark, a moving wave, and
**Loading…**. Opening or refreshing the URL covers the whole screen. When you
switch pages using the menu or the browser's Back and Forward buttons, it covers
the content below the header, leaving the menu and its dividing line visible.
It also appears when you return to a page you have already visited. Controls
appear directly in their current positions, without animating during loading or
the fade. As soon as the page's data is ready, the loading view fades away in a
quarter of a second.
Control animations work normally once the page is visible. Stats, History, and
Settings use this single loading view rather than separate loading animations
inside their sections.
On first opening a page, its content and fresh data load together. Returning to
a page reuses its content and controls while fetching fresh data. Stats and
History request their current status and records together, and the loading view
waits for both before fading.
If loading fails, the page shows an error. If this browser loses its claim, the
loading view hands over to **Reload**.

**Add to home screen** installs the page like an app. On iPhone or iPad, open
the controller page in Safari, tap **Share**, then **Add to Home Screen**; from
then on it opens full screen with the Open Brew by Weight icon — the brand mark
on the brand colour, edge to edge, so it fills its place on the home screen
without a white frame around it. On Android, open the controller page in Chrome,
tap the **⋮ menu**, and choose **Add to Home screen**. Because
the controller is reached directly at its local address, browsers do not offer
an automatic install prompt — the menu option above is the installation. The
icon is taken from the page when you add it, so an icon added before a firmware
update keeps the artwork it was installed with: remove it and add the page again
to pick up the current one.

**Out of reach**: when the page opens but the controller cannot be reached —
for example while you are away from home — it shows the Open Brew by Weight logo with
an out-of-reach message and a **Reload** button instead of a raw error. The same
message appears if the connection drops while the page is open; in that case
the page resumes on its own once the controller answers. When the page opens
straight into the message, check that the phone or computer is connected to the
same network as the controller, then press **Reload**. Right after a firmware
update the message can appear while the controller restarts; wait a moment and
press **Reload**. If the browser has no saved copy of the page yet, it shows
its own cannot-connect page instead.

**Admin unlock** is an additional password check for privileged actions.
The **Firmware** section in Admin holds a master switch that can turn every
brewing feature off and leave the controller as a transparent pass-through;
see [firmware mode](settings/firmware-mode.md).
It stays renewed while Admin is open, or expires 15 minutes after the last
privileged action. **Lock** closes it immediately without releasing the browser
claim. Most configuration changes require an idle machine.

Open any Web UI page to set up the time zone from this phone or computer when
the controller is idle; unlocking Admin is not required. Setup continues until
the zone has been saved successfully. In **Admin → Date and time**, automatic
mode follows the device using the UI. Turn it off to choose a zone manually,
check the preview, and save your settings. See
[Time zone and NTP](settings/wifi.md#parameters-and-behavior).

Save buttons in **Settings** and **Admin** stay dimmed until you change one of
their values. When you save, the small icon on the button turns into a spinning
ring while the controller applies the change. A successful save dims the button
again; if saving fails, the button remains available so you can retry and the
red message bar explains the error. Every save button has a smaller revert
button beside it with a circular arrow. It lights up together with the save
button, and tapping it asks for confirmation and then puts the section's fields
back to their last saved values, so you can walk away from edits you did not
mean to keep.

Remote start/rinse are disabled in default firmware. Admin unlock does not
enable them. Remote Stop is privileged; physical controls remain available
according to the selected [paddle](settings/paddle.md) or
[momentary](settings/momentary.md) behavior.

## Connect the scale and choose a recipe

1. Check Home for a connected scale and a fresh weight reading. A Bluetooth
   connection alone is not sufficient for automatic weight control.
2. On a new controller, **Prefer selected → First detected** adopts the first
   compatible scale that connects successfully and then prefers it. To replace
   it or use several scales, follow [Scales](settings/scales.md).
3. In **Settings → Brew**, load **Double** or **Single**. For your own recipe,
   duplicate a preset, change the target and guards, and save.
   [Presets](features/presets.md) lists the factory values.
4. For a latch/paddle installation, start with **Natural**, the default mode.
   Momentary builds show **Switch** instead; use their
   [button and sensor settings](settings/momentary.md).
5. Leave Quick rinse off until you have tried its
   [gesture deliberately](settings/quick-rinse.md).

## First shot

With a usable scale, BBW enabled, the factory Double preset, Natural paddle
mode, and Quick rinse off:

1. Place your cup on the scale.
2. Move the paddle ON. The controller tares the scale and starts the timer.
3. Leave it ON while brewing. The nominal goal is 36 g; the controller
   compensates for dripping, and Fast/Slow guards may change the endpoint.
   To finish early in Natural mode, move the paddle OFF.
4. After automatic stop, return the paddle to OFF before starting again.
5. Leave the cup on the scale through the drip delay (default 3 s). The final
   weight then updates Last Shot and, if eligible, history.

While brewing, Home advances **Dur** in whole seconds. When the shot ends,
Last/Current shot immediately shows the final duration with one decimal place.

On a **momentary** machine, press and release to start; another valid press
requests stop. Automatic stop uses a pulse, not a continuously open contact.
Without reed feedback, the running state is inferred and can be wrong.
Read [Stopping and time limits](settings/momentary.md#stopping-and-time-limits)
before relying on automatic operation.

## Common next steps

| Situation | What to do |
| --- | --- |
| Cup placed after starting | Place it within the [retare window](features/tare-retare.md); later placement is not automatically corrected. |
| Shot ends above or below target | Read the stop detail in [history](features/shot-history.md), then check Fast/Slow and learned offset. |
| Scale unavailable before start | Follow [No-scale BBW](settings/no-scale-bbw.md); the default first attempt warns and blocks. |
| Scale disconnects during brewing | Weight stop pauses; [A→M](features/auto-to-manual.md) may request a timed stop while reconnection continues. |
| Need a timer-only session | Turn BBW off in Home Quick Settings. It does not rewrite the saved recipe. |
| Cannot save a setting | Finish the cycle, return the activator to idle, reclaim the UI if necessary, and check Admin unlock for privileged settings. |
| Cannot reach the controller | Use [network troubleshooting](FAQ.md#network-and-access) before considering a reset. |

Next: [settings index](README.md#settings) · [troubleshooting](FAQ.md) ·
[updates](features/ota.md).
