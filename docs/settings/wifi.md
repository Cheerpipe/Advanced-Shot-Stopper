# Wi-Fi

How the controller joins your home network (STA) and when it falls back to
its own access point. Details for the fallback AP itself are in [AP](ap.md).

The Web UI is reachable from any client on the same network. Home and
Settings use a single-browser claim (`Reload`); see
[Web access](../GETTING_STARTED.md#web-access). **Admin** is additionally locked
behind the device password. Unlock stays active while that Admin page is
open, or for 15 minutes after the last privileged action; **Lock** closes
it immediately. Use a trusted network.

## When it applies

Wi-Fi and the HTTP server start regardless of paddle position.
**Configuration changes** still need a maintenance window: paddle OFF, machine circuit
open, no active cycle.

You cannot change workflow settings during an active shot. Read-only status
stays available.

## Parameters and behavior

| Setting / behavior | Default | Effect |
| --- | --- | --- |
| **Home Wi-Fi (STA)** | none on a fresh flash | Saved SSID/password. Device joins your network and serves the Web UI at the STA IP. |
| **Wi-Fi sleep** | on | When on, STA uses modem sleep (`MIN_MODEM`) whenever it is associated and SoftAP is down. Stays `NONE` on SoftAP, during OTA, while STA is disconnected, or when this checkbox is off. It remains independent of [Power management](power-management.md), WebUI activity, and scale connection state. Factory default on; V1 migration stays off. Scale discovery duty is Admin → Bluetooth → Scan intensity, not this checkbox. |
| **IP mode** | DHCP | **DHCP** or **static** (`ip` / `netmask` / `gateway` / `dns1` / `dns2`). |
| **Confirm window** | 3 minutes | After a Web UI STA save, a wait overlay retries the current address for confirmation. If the network/IP changed, reconnect your client and open the new IP yourself. The first successful UI claim confirms the new network. If this page never returns, previous network settings are restored (BLE Companion saves use the same window: open the UI at the new IP). USB `SET_WIFI` commits immediately. |
| **Boot with no credentials** | SoftAP up | SoftAP at boot with a **3 min** idle shutdown when no SoftAP stations are associated. See [AP](ap.md). |
| **Boot with credentials** | STA first | SoftAP only if STA does not associate in about **25 s** (and STA never joined this boot). Then AP+STA until STA connects or SoftAP idle-stops; SoftAP is then stopped. |
| **STA drops after a successful join** | retry STA only | SoftAP is **not** raised automatically. Use USB `AP_START` or reboot. |
| **Timezone offset (min)** | UTC+0 | Wall-clock labels in shot history. |
| **NTP server** | pool | Preset or custom hostname for time sync. |

Factory credentials and the first-connection walkthrough are in the
[README](../../README.md#first-connection) and [AP](ap.md).

Scan lists up to **12** networks. The firmware scan operation has a **20 s**
timeout; the browser's overall wait can be longer. Cancel from
the same maintenance window.

## Example

From the AP, save your home network. When the controller joins, its AP stops.
Join the home network on your phone/computer, find the controller's DHCP address
in the router or USB `NET_STATUS`, and open that address within 3 minutes to
confirm. The old AP page cannot discover an arbitrary new DHCP address.
If you later lose that network, the device keeps retrying STA. Recover the AP with USB
`AP_START` (see [USB serial CLI](../SERIAL_CLI.md)) or a reboot.

OTA over Wi-Fi: [OTA](../features/ota.md). Scripts:
[Build scripts](../SCRIPTS.md).

Related: [AP](ap.md), [Factory reset](factory-reset.md).
