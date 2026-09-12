# First setup and daily use

Use this guide after firmware installation and the applicable
[bench checks](MANUAL_TEST_PLAN.md). For equipment and wiring prerequisites,
start with [Hardware](HARDWARE.md).

## Connect for the first time

1. Keep the physical activator OFF (release a momentary button) and power the
   controller. Turn on the scale with no other app connected to it.
2. Join the controller's [fallback access point](settings/ap.md#first-connection)
   from your phone or computer. Stay connected even if the phone reports
   "no internet"; open the AP address in a browser.
3. If the Web UI shows **Reload**, select it to claim this browser session.
   Open **Admin** and unlock it with the device password.
4. Change the factory device password in **Admin → Device password**. It is
   shared by Admin, OTA, and the access point. If the AP disconnects after a
   password change, reconnect using the new password.
5. In **Admin → Wi-Fi**, enter your home network and save. Rejoin that network
   on your phone/computer when the controller's AP closes.
6. Open the controller at its new IP address within the 3-minute confirmation
   window. Find it in the router's connected-device list or via
   [USB NET_STATUS](SERIAL_CLI.md). The old AP address does not automatically
   become a home-network address. A successful browser claim confirms the new
   settings; unreachable settings revert.

You can also operate locally over the AP while your client remains associated.
The [AP guide](settings/ap.md) explains its idle shutdown and recovery.

## Web access

**Claim / Reload** gives one browser control of Home and Settings. Another client
can take the claim, and 15 minutes without using a control locks the page and
stops polling. Scrolling does not keep it active; use Reload to resume.

**Admin unlock** is an additional password check for privileged actions.
It stays renewed while Admin is open, or expires 15 minutes after the last
privileged action. **Lock** closes it immediately without releasing the browser
claim. Most configuration changes require an idle machine.

Save buttons in **Settings** and **Admin** stay dimmed until you change one of
their values. A successful save dims the button again; if saving fails, the
button remains available so you can retry and the red message bar explains the
error.

Remote start/rinse are disabled in default firmware. Admin unlock does not
enable them. Remote Stop is privileged; physical controls remain available
according to the selected [paddle](settings/paddle.md) or
[momentary](settings/momentary.md) behavior.

## Connect the scale and choose a recipe

1. Check Home for a connected scale and a fresh weight reading. A Bluetooth
   connection alone is not sufficient for automatic weight control.
2. On a new controller, **Preferred only → First detected** adopts the first
   compatible scale that connects successfully. To replace it or use several
   scales, follow [Scales](settings/scales.md).
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
