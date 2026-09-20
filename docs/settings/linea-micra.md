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
4. Choose **Use selected machine**. Only then are monitoring and Micra preset
   options enabled and saved.

If the account returns no Linea Micra machines, the account is not enabled and
the machine-specific options remain unavailable. Accounts with several Micras
show each returned name and serial number so the intended machine can be
selected explicitly.

The email, password, selected machine, and device installation key are stored
in the controller so it can sign in again after a restart. Short-lived access
and refresh tokens stay only in RAM and are never returned by the Web UI or
diagnostics. **Disconnect** removes the saved account credentials, installation
key, selected machine, cached list, and in-memory session tokens.

The cloud interface used by the La Marzocco app is not a documented public API
and can change independently of this firmware. A cloud outage or API change
affects Micra monitoring, but never scale operation, paddle handling, relay
safety, or local shot control.

## Monitor machine power state

Enable **Monitor machine power state** and save. Shot Stopper then queues a
dashboard read approximately every 15 seconds while STA is connected. It never
starts a request in setup AP mode. A shot or rinse cancels an in-flight read and
pauses new state reads; one becomes due again after local activity ends.

Diagnostics → Machine shows:

| Micra response | Displayed state |
| --- | --- |
| `StandBy` | OFF |
| `BrewingMode` | ON |
| Other valid mode, stale sample, or communication failure | UNKNOWN |

A confirmed sample is current for 30 seconds. The firmware and browser replace
an expired ON or OFF display with UNKNOWN. Failures use bounded 3, 6, and 9
second retry delays; after four failed attempts automatic reads wait at least
60 seconds. **Refresh state** adds a read to the same bounded queue and cannot
bypass STA, AP, shot, busy, or cooldown rules.

UNKNOWN is treated as effectively on only for the conservative diagnostic
policy. The observed state never starts or stops the machine, gates a shot,
changes a preset, or changes paddle behavior.

## Brew temperature in presets

**Allow brew boiler temperature in presets** reveals a per-preset value from
80.0 to 100.0 °C in 0.1 °C steps. New and migrated presets start at 93.0 °C;
duplicates copy the source value. Turning the option off or disconnecting the
account keeps every saved preset value.

The current read-only feature stores these values and reports the selected
machine's target temperature, but it does not yet apply preset temperatures to
the machine. Use the La Marzocco app to change the boiler target.

Factory reset removes the Micra cloud account, selected machine, installation
key, options, and RAM session. Preset temperatures return to 93.0 °C. See
[Factory reset](factory-reset.md) and [Presets](../features/presets.md).
