# LineaMicraBLE

`LineaMicraBLE` is the native NimBLE peer client and bounded protocol codec used
by ShotStopper's compile-time Linea Micra integration. It borrows the already
running `ShotStopperBleRuntime`; it never initializes, resets, stops or owns the
NimBLE host.

The library deliberately exposes only:

- authentication with an exact 64-character printable token;
- `machineCapabilities`, `boilers` and `machineMode` reads;
- a `SettingBoilerTarget` write for `CoffeeBoiler1` in the 80.0–100.0 °C range;
- command-result reads after an application-requested target write.

The application owns peer discovery and binding, scheduling, retries, settings,
logging and UI state. It passes a verified address to `connect()`, advances the
client with `service()`, consumes bounded events with `takeEvent()`, and calls
`abort()` or `disconnect()` when its generation is cancelled. The client does
not create a task, scan, persist data, use the network or interact with the
relay. Callback generations discard stale asynchronous completions.

Responses are limited to 512 bytes and parsed with strict type, duplicate-field,
depth and range checks. Tokens are never returned in events. The client assumes
that all calls are serialized by one application owner; it is not a general
multi-owner BLE scheduler.

Protocol details were independently implemented from public interoperability
references, principally
[`pylamarzocco` at commit 1f43ae0](https://github.com/zweckj/pylamarzocco/tree/1f43ae0880b902c38b3d7f5974fda1d296c351bf)
and the native ESP32 transport in
[`kaspizzo/lamarzocco` at commit 2552b14](https://github.com/kaspizzo/lamarzocco/tree/2552b14eff7147986b434ab93d69ee29e710b4f8).
The test payloads in this repository are synthetic and contain no device token.
