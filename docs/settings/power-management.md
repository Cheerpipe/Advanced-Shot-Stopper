# Power management

**Admin → Frontend → Power management** enables a global, persistent energy
policy. It defaults **on**, including after migration and factory reset, and
does not belong to a shot preset. Save it while the machine is stopped; the
existing Admin unlock and configuration revision checks apply.
Admin confirms the applied revision and selected value, then waits for the
asynchronous configuration save to finish. An `APPLIED` command result alone
does not confirm durable storage. Admin status exposes `config.persistPending`
and `config.persistFailed`; a failed write is reported without undoing the live
setting, and the existing persistence worker retries it.

| Demand with the option on | CPU policy | Radio policy |
| --- | --- | --- |
| Idle, no scale or machine activity | 40–80 MHz after 1 s of stable idle | Light scan duty (25%); BLE controller modem sleep between radio events; STA Wi-Fi MIN_MODEM |
| Scale connecting or connected | Fixed 80 MHz | Controller sleep disabled before GAP connection; existing GATT, weight and heartbeat rates |
| Shot using weight control | Fixed 160 MHz | Existing Bluetooth-priority coexistence and shot traffic gates |
| Manual operation without a scale, or rinse | Fixed 80 MHz throughout the operation | Normal scan preference and BLE service |
| Physical-use cooldown | Fixed 80 MHz for 5 minutes after confirmed stop or latest debounced physical edge | Saved Wi-Fi sleep preference; normal scan preference |
| Recent visible WebUI activity | Fixed 80 MHz unless a shot needs 160 | Responsive Wi-Fi (PS_NONE) |
| AP provisioning, STA reconnect, maintenance or USB console | At least 80 MHz | Existing provisioning/OTA overrides |

Forty MHz is an eligible minimum: radio drivers can hold the CPU at 80 MHz.
The Admin status shows the instantaneous CPU clock and configured range, not
measured residency or electrical consumption. Board measurements are required
to quantify savings and qualify scale-discovery, HTTP and control latency.

Machine state determines how long operation remains protected. An open relay
alone does not prove a momentary machine is stopped. A weighted shot retains
its 160-MHz requirement after scale loss until confirmed completion. The
five-minute cooldown never ends or extends a shot; existing safety limits,
including the 60-second hard cap, remain unchanged.

Opening or returning to the visible WebUI, and trusted taps, input, keys or
scroll gestures, create a three-minute activity window. Existing requests send
`X-WebUI-Activity` with at most 30 seconds of remaining presence. Automatic
polls do not extend human activity; hidden/closed/disconnected pages stop renewal
and the controller releases the WebUI requirement within 30 seconds. The last
lease is capped by the human activity deadline. No unload request is needed.
This is independent of the existing 15-minute WebUI ownership inactivity rule
and of the physical-use cooldown. The highest current CPU demand wins.

With the option **off**, CPU stays at 80 MHz, controller modem sleep is disabled,
and the saved scan intensity and Wi-Fi sleep preferences apply. PM support is
still compiled in, so its SDK overhead remains. Neither mode uses automatic
light sleep or deep sleep. The independent safety timer stays enabled on XTAL;
speaker LEDC also uses the S3 crystal clock.

Admin and Diagnostic status include a `power` object with the enabled setting,
requested/applied profiles, instantaneous `cpuMhz`, `minMhz`, `maxMhz`, apply
errors, BLE sleep policy, WebUI presence and remaining physical cooldown. An optional clock apply
failure latches a fixed-80-MHz fallback until reboot. If even that fallback
fails, the existing safety trip/restart path applies. A rejected BLE sleep-enable
request disables optional savings and restores awake service. Failure to disable
sleep, or a controller still asleep after 100 ms, reports a critical task fault
through the existing relay safety trip/restart path. Wake waits yield without
publishing worker progress or advancing BLE work, including when no scale is
connected. Errors remain visible in status.
