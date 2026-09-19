# La Marzocco Linea Micra

Firmware built for the **La Marzocco Linea Micra** shows its controls in
**Settings → Machine and scale → La Marzocco Linea Micra**. Other machine
profiles omit the panel and its diagnostics.

## Pair the machine

1. Obtain the 64-character BLE token for your own Micra.
2. Copy the Micra's exact Bluetooth address from Home Assistant's Bluetooth
   advertisement monitor, in the form `AA:BB:CC:DD:EE:FF`.
3. Enter both values under **BLE token** and **Micra Bluetooth address**, choose
   **Save Micra settings**, and wait for the save to finish.
4. Choose **Test connection** while Shot Stopper is Ready.
5. Confirm that **Paired machine** shows the verified Micra and that the
   communication result includes measured and target boiler temperatures.

Test is read-only: it authenticates, verifies the Micra family, and reads the
brew boiler. It never changes power or temperature. The token is write-only in
the Web UI and diagnostics; leaving the field blank keeps the saved token.

The address directs every pairing attempt to that device only. Shot Stopper
still authenticates it, verifies the Micra protocol, and reads the brew boiler
before saving it as paired. It never connects to unrelated anonymous Bluetooth
devices. The address may be left blank only when exactly one nearby device
advertises a `MICRA_*` name; zero or multiple named Micras fail safely instead
of selecting one by signal strength.

A scale candidate always has radio priority. Home Assistant or the La Marzocco
app may temporarily occupy the machine connection, so a Test can need another
attempt without affecting brewing.

**Forget machine** removes the paired/configured address but keeps the token,
options, and preset temperatures. **Remove token** also removes the address and
pairing. Neither action changes paddle, relay, or shot behavior.

## Monitor machine power state

Enable **Monitor machine power state** and save. Shot Stopper then reads the
Micra mode approximately every 15 seconds when shared Bluetooth activity
allows it, and requests a fresh read after critical brew/rinse activity ends.
The setting is off by default.

Diagnostics → Machine shows:

| Micra response | Displayed state |
| --- | --- |
| `StandBy` | OFF |
| `BrewingMode` | ON |
| Other valid mode, stale sample, or communication failure | UNKNOWN |

A confirmed sample is current for 30 seconds. The firmware and browser both
replace an expired ON or OFF display with UNKNOWN, so a disconnected browser
cannot leave an old OFF result looking current. Connection failures use bounded
3, 6, and 9 second retry delays; exhaustion pauses automatic reads for at least
60 seconds. **Refresh state** requests another read but does not bypass safety,
radio-priority, or cooldown rules.

UNKNOWN is treated as effectively on only for the conservative diagnostic
policy. The observed state never starts or stops the machine, gates a shot,
changes a preset, or changes paddle behavior.

## Brew temperature in presets

**Allow brew boiler temperature in presets** reveals a per-preset value from
80.0 to 100.0 °C in 0.1 °C steps. New and migrated presets start at 93.0 °C;
duplicates copy the source value. Turning the option off or removing the token
keeps every saved value.

The current read-only state feature stores these preset values and exposes them
in the Micra-specific UI, but it does not yet apply them to the machine. Until
automatic target reconciliation is enabled and confirmed, use the La Marzocco
app or another supported control path to change the boiler target.

Factory reset removes the token, pairing, and options and restores preset
temperatures to 93.0 °C. See [Factory reset](factory-reset.md) and
[Presets](../features/presets.md).
