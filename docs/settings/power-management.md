# Power management

The **Admin → Power management** group holds the five settings that control
how much energy the controller uses, in this order:

- **Power policy** — hardware-level energy management described on this page.
  Scales the CPU clock and bus/radio sleep to demand: on saves energy when
  idle and boosts to 160 MHz for weight-controlled shots; off holds a fixed
  80 MHz.
- **Wi-Fi sleep** — puts the Wi-Fi radio into modem sleep between the
  router's beacons while connected. Saves immediately without restarting or
  waiting for a reconnect, and stays disabled until a network is configured.
  [Discovery by name](wifi.md#discovery-by-name) keeps working while sleep is
  on: the radio wakes for every beacon window, so name lookups stay slightly
  delayed but are not lost. Details in [Wi-Fi](wifi.md).
- **BLE scan mode** — how much radio time is spent searching for Bluetooth
  espresso scales: **Aggressive**, **Balanced** (factory default), or
  **Relaxed**. See [Scales](scales.md).
- **Idle scan backoff** — with Aggressive or Balanced selected, after this
  many idle minutes with no scale in range the search drops to Relaxed until
  a scale appears. **OFF** (factory default) keeps the saved mode always.
  The control is grayed out while Relaxed is selected because it has no
  effect then.
- **Scan boost on machine use** — the reverse of the backoff: when the
  machine is switched on (paddle or momentary) with no scale connected, the
  search runs at Aggressive for this many minutes so a scale that was put to
  sleep is found quickly. Switching the machine on again restarts the window,
  and the boost takes priority over the Relaxed slowdowns on this page for as
  long as it lasts. **OFF** (factory default) never boosts.

The **Power policy** enables a global, persistent energy
policy. It defaults **on** on a clean install and after factory reset, and
does not belong to a shot preset. Save it while the machine is stopped; the
existing Admin unlock and configuration revision checks apply.
Admin confirms the applied revision and selected value, then waits for the
asynchronous configuration save to finish. An `APPLIED` command result alone
does not confirm durable storage. Admin status exposes `config.persistPending`
and `config.persistFailed`; a failed write is reported without undoing the live
setting, and the existing persistence worker retries it.

| Demand with the option on | CPU policy | Radio policy |
| --- | --- | --- |
| Idle, no scale or machine activity | 40–80 MHz after 1 s of stable idle | Relaxed scan duty (25%); BLE controller modem sleep between radio events; saved Wi-Fi sleep preference; a Scan boost on machine use window overrides the duty to Aggressive while it lasts |
| Scale connecting or connected | Fixed 80 MHz | Controller sleep disabled before GAP connection; existing GATT, weight and heartbeat rates |
| Shot using weight control | Fixed 160 MHz | Existing Bluetooth-priority coexistence and shot traffic gates |
| Manual operation without a scale, or rinse | Fixed 80 MHz throughout the operation | Saved scan intensity and BLE service; switching the machine on without a connected scale also opens a Scan boost on machine use window if the setting is not OFF |
| Physical-use cooldown | Fixed 80 MHz for 5 minutes after confirmed stop or latest debounced physical edge | Saved Wi-Fi sleep preference; saved scan intensity |
| Recent visible WebUI activity | Fixed 80 MHz unless a shot needs 160 | Saved Wi-Fi sleep preference |
| AP provisioning, STA reconnect, maintenance or USB console | At least 80 MHz | Existing provisioning/USB overrides |

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
and the saved scan intensity applies. The saved Wi-Fi sleep preference applies
independently in both modes. PM support is still compiled in, so its SDK overhead
remains. Neither mode uses automatic
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
