# AP

Fallback Wi-Fi access point used when no home network is saved, or when saved
STA fails to associate at boot.

## When it applies

- Fresh flash or [factory reset](factory-reset.md): SoftAP is up at boot.
- Saved STA fails to associate for about **25 s** at boot: SoftAP comes up
  and STA keeps retrying in AP+STA until STA connects. Then SoftAP stops.
- SoftAP auto-raise is **boot/bootstrap only**. After a successful STA join,
  a later drop does **not** raise SoftAP. Use USB `AP_START` or reboot.
  See [USB serial CLI](../SERIAL_CLI.md).

## Idle shutdown

Auto SoftAP stays up for at most **3 minutes** with zero SoftAP stations.
If a phone or laptop joins the AP, SoftAP stays up while that client is
associated. When the client count returns to zero, a new 3-minute idle
countdown starts. After idle shutdown, SoftAP stays down for the rest of
the boot (USB `AP_START` or reboot to raise it again). Manual `AP_START`
keeps SoftAP up without the idle timer until `AP_STOP`.

The AP name is unique to this controller: **`OpenBrewByWeightAP-`** plus
eight lowercase characters from the end of this device's AP address. USB
`AP_STATUS` prints the exact name. SoftAP WPA2 uses the **device password**.
Change it from **Admin** (unlock with the device password) **→ Device password**
or from USB (`SET_DEVICE_PASSWORD` / `RESET_DEVICE_PASSWORD`). Admin unlock,
and OTA use that same device password.

## Parameters

| Setting | Default | Notes |
| --- | --- | --- |
| **AP name** | `OpenBrewByWeightAP-xxxxxxxx` | Not user-editable. The eight characters are unique to this controller. USB `AP_STATUS` shows the live name. |
| **Device password** | `ineedacoffee` | Case-sensitive. 8–63 characters when you change it; USB `SET_DEVICE_PASSWORD` will not accept the factory string as the new value. SoftAP WPA2, Admin unlock, and OTA all use this same device password. |
| **AP address** | `http://192.168.4.1` | SoftAP IPv4. |

## First connection

1. Power the ESP32-S3 and wait for boot (the GPIO 1 LED stays off until a
   scale connects).
2. Join **`OpenBrewByWeightAP-xxxxxxxx`** (the unique name on this
   controller; USB `AP_STATUS` prints `ssid=`) with the device password
   **`ineedacoffee`**. Forget any saved network that is only
   `OpenBrewByWeightAP` with no suffix. The scale may wait to connect
   until you finish this setup network.
3. The setup page opens by itself: pick your home network, enter its
   password, and connect. If nothing pops up, open
   **`http://192.168.4.1/setup`** in a browser. Joining this network already
   proves the device password, so the setup page asks for nothing else. The
   controller joins your network while this setup network stays up, then
   shows the addresses to use from now on and keeps them on screen for about
   a minute so you can note them; **Done** on that screen closes the setup
   network right away. If the connection fails, the page
   keeps your entries so you can retry; on a first-time setup, giving up
   leaves the controller waiting on this same setup network for another
   try (it forgets the failed attempt after about 3 minutes).
4. Continue with [first setup](../GETTING_STARTED.md). Changing networks
   later (including a static IP address) is done from
   **Admin → Wi-Fi** on your home network.

While you are on this AP, every name resolves to the controller, and phones
and computers detect that sign-in is required — that is what makes the guided
setup page open on its own. The controller's
[device name](wifi.md#discovery-by-name) also resolves while you are here.

Not ready to configure Wi-Fi yet? Choose **Continue without Wi-Fi** on the
setup page. The sign-in window closes on its own, but your phone stays
joined to the setup network and the setup page remains open at
`http://192.168.4.1` until the setup network itself shuts down — reopen it
there whenever you want to continue.

If home Wi-Fi is lost but you know the device password, use the AP after
reboot / `AP_START`: it serves the same guided setup page, and the whole
flow is passwordless while you are on it. A forgotten password also prevents
joining the protected AP: use USB or
[physical recovery](../EMERGENCY_RECOVERY.md) to restore access.

Related: [Wi-Fi](wifi.md), [Factory reset](factory-reset.md).
