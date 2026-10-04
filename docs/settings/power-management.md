# Power management

The **Admin → Power management** group holds the three settings that control
how much energy the controller uses, in this order. Like every other Admin
group, changes are kept in the page only until you press the group's
**Save settings** button; the arrow button beside it discards them and puts
the saved values back. Only the fields you changed are saved.

- **ESP32 power management** — hardware-level energy management described on
  this page. Scales the CPU clock and bus/radio sleep to demand: on saves
  energy when idle and boosts to 160 MHz from the start of a scale connection
  until 30 seconds after disconnection; off holds a fixed 80 MHz. It manages
  the ESP32's hardware only — Bluetooth search intensity is controlled by the
  BLE scan mode below and, on a La Marzocco Linea Micra, by the
  [machine-aware scan options](linea-micra.md#machine-aware-scale-search).
- **Wi-Fi sleep** — puts the Wi-Fi radio into modem sleep between the
  router's beacons while connected. Saves without restarting or waiting for a
  reconnect, and stays disabled until a network is configured.
  [Discovery by name](wifi.md#discovery-by-name) keeps working while sleep is
  on: the radio wakes for every beacon window, so name lookups stay slightly
  delayed but are not lost. Details in [Wi-Fi](wifi.md).
- **BLE scan mode** — how much radio time is spent searching for Bluetooth
  espresso scales: **Aggressive**, **Balanced** (factory default), or
  **Relaxed**. The saved mode always decides discovery duty, whatever the CPU
  power profile is. See [Scales](scales.md). The separate
  [Bluetooth on/off](scales.md#bluetooth-onoff) switch in the Admin **BLE**
  group can stop all scale Bluetooth regardless of this mode.

**ESP32 power management** enables a global, persistent energy
policy. It defaults **on** on a clean install and after factory reset, and
does not belong to a shot preset. Save it while the machine is stopped; the
existing Admin unlock and configuration revision checks apply.
When you press the group's Save button, Admin confirms the applied revision
and selected value, then waits for the
asynchronous configuration save to finish. An `APPLIED` command result alone
does not confirm durable storage. Admin status exposes `config.persistPending`
and `config.persistFailed`; a failed write is reported without undoing the live
setting, and the existing persistence worker retries it.

| Demand with the option on | CPU policy | Radio policy |
| --- | --- | --- |
| Idle, no scale or machine activity | 40–80 MHz after 1 s of stable idle | BLE controller modem sleep between radio events; saved Wi-Fi sleep preference; the saved BLE scan mode keeps running unchanged |
| Scale connecting, connected, or within 30 seconds after disconnection | Fixed 160 MHz | Controller sleep disabled throughout the window; existing GATT, weight and heartbeat rates |
| Manual operation or rinse outside the scale window | Fixed 80 MHz throughout the operation | Saved scan intensity and BLE service |
| Physical-use cooldown outside the scale window | Fixed 80 MHz for 5 minutes after confirmed stop or latest debounced physical edge | Saved Wi-Fi sleep preference; saved scan intensity |
| Recent visible WebUI activity outside the scale window | Fixed 80 MHz | Saved Wi-Fi sleep preference |
| AP provisioning, STA reconnect, maintenance or USB console | At least 80 MHz | Existing provisioning/USB overrides |

Forty MHz is an eligible minimum: radio drivers can hold the CPU at 80 MHz.
The Admin status shows the instantaneous CPU clock and configured range, not
measured residency or electrical consumption. Board measurements are required
to quantify savings and qualify scale-discovery, HTTP and control latency.

Machine state determines how long operation remains protected. An open relay
alone does not prove a momentary machine is stopped. A scale reconnect starts
a new 160-MHz window, and the clock returns to the normal operating range
30 seconds after the final disconnection, even if a shot continues. The
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

CPU profiles, cooldowns and grace windows manage hardware only: they never
select or override the Bluetooth search intensity. That intensity comes from
the saved BLE scan mode and, when it applies, from the Micra
machine-aware scan options.

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
