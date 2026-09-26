# EspressoScaleBLE
Native ESP-IDF NimBLE gateway for espresso scales on ESP32-S3 devices with PSRAM.

Maintained by **Felipe Urzúa** (`cheerpipe@gmail.com`) as part of
[Cheerpipe/AcaiaArduinoBLE](https://github.com/Cheerpipe/AcaiaArduinoBLE).
The original AcaiaArduinoBLE library was created by Tate Mazer; see
[Acknowledgement](#acknowledgement).

## Scale Compatibility

Ratings and listed firmware versions are retained observations, not a promise
that every model was requalified on the current build. **Testing** means the
protocol is implemented but target validation remains incomplete; **Untested**
means no result is recorded for that capability. For daily setup, use
[Scales](../../docs/settings/scales.md).

| Mfr    | Model   | Submodel | Firmware | Connection Performance | Auto-Tare | Auto-Start/Stop Timer | Auto-Reset Timer |
| ------ | ------- | ------- |------- |------ | ------ |------ |------ |
| Acaia  | Lunar   | USB-Micro <br>(Pre-2021) | v2.6.019 | Great | Yes | Yes | Untested |
| Acaia  | Lunar   | USB-C <br> (2021 version)  | v1.0.016 | Hit or Miss    | Yes | Yes | Yes |
| Acaia  | Pearl S | USB-Micro                  | v1.0.056 | Ok    | Yes | Yes | Yes |
| Acaia  | Pearl S | USB-C                      | ----     | Ok    | Yes | Yes  | Yes |
| Acaia  | Pyxis   | ----                       | v1.0.022 | Good  | Not Recommended (too sensitive) | Yes | Yes |
| Bookoo | Themis  Mini | ----                       | v1.0.5   | Great | Yes | Yes | Yes |
| Bookoo | Themis Ultra  | ----                 | ----   | Great | Yes | Yes | Yes |
| Felicita | Arc   | ----                       | ----   | ---- | Yes | Yes | Yes |
| AtomHeart | Eclair | ----                      | v2.1.0 | Testing | Yes | Yes | Yes |
| Decent | Scale / EspressiScale | ----              | ----   | Testing | Yes | No | No |
| DiFluid | Microbalance / Ti | ----                 | ----   | Testing | Yes | No | No |
| MyScale | KP2048B | ----                      | ----   | Testing | Yes | No | No |
| Varia | AKU / Mini / Pro | ----                   | ----   | Testing | Yes | Yes | Yes |
| Eureka | Precisa (`CFS-9002`, `LSJ-001`) | named GAP only | ---- | Testing | Yes | Yes | Yes |
| WeighMyBru | ---- | ----                      | ----   | Testing | Yes | No | No |

Not supported: Timemore Black Mirror DUO, Timemore Dot, Acaia Umbra, Eureka
Precisa units that advertise no GAP name (manufacturer-data only). Weight is
always reported in grams.

Scales without timer or volume bits follow the Eclair-like firmware path:
tare if present, local buzzer for alerts, no combined tare+start.

## Scale power-off command

`EspressoScaleBLE::powerOff()` switches a connected scale off when its
protocol implements a power-off command (`ScaleFeaturePowerOff`). Only the
Bookoo protocol family carries the feature today: BooKoo's published
contract defines shutdown command `0x15` (`03 0A 15 00 00 1C`) for the
Themis Ultra with firmware V4.0.0 and later (ignored while charging and by
V3.1.2 and earlier). The Themis Mini contract has no shutdown command, and
both models advertise as `BOOKOO`, so the command is sent to the family and
ignored by units that do not implement it. Every other protocol reports
`ScaleCommandResult::Unsupported`. Once shutdown is requested, that BLE
connection generation is closed to every later application command, even if
the scale takes time to disconnect. The three-second communication barrier is
armed immediately before this terminal write. A later GAP disconnect callback
marks the link lost before the application can observe it and restarts the full
interval after any already-admitted radio submission returns.


## Requirements

Release 5.0.0 uses the native NimBLE C APIs from ESP-IDF 6.1.x. The supported
integration is the IDF component in this repository, pinned to ESP-IDF 6.1
and Arduino-ESP32 3.3.11. It targets ESP32-S3 n8r4 and n16r8 boards; standalone
Arduino Library Manager and SAMD builds are no longer supported.

The BLE lifecycle is asynchronous and deadline-bounded. The runtime owns the
single NimBLE host, while the scale worker remains the sole owner of scale
state. Pair the library with task watchdogs and fail-open outputs; it is not a
standalone machine-safety mechanism. See the project
[concurrency model](../../docs/CONCURRENCY.md).

## Robust connection behavior

Discovery, connection, GATT resolution, subscription and writes run through a
bounded native NimBLE state machine. Every asynchronous operation is tied to a
connection generation and deadline; late callbacks cannot revive a discarded
connection. Cleanup is idempotent, eight consecutive invalid notifications
force a recoverable disconnect, and the first-valid-packet and silence limits
remain protocol-specific.

Cancelling a pending connection retains one cleanup operation until GAP reports
failure or confirms disconnection. If connection success wins the cancellation
race, the owner terminates that link; callbacks never submit the termination.
New scans and connections wait for that acknowledgement, including after the
quiet interval. A missing acknowledgement leaves reconnection blocked until a
host reset; rejected termination attempts are spaced by the quiet interval.
Host reset discards the old cleanup operation so a reused handle is untouched.

Keep the facade alive while the BLE runtime is active. Destruction drains
callbacks already using the object and prevents access after destruction, but
cannot reconcile a connection success delivered after callback ownership has
been released. Dynamic destruction during connection setup is therefore not a
supported way to guarantee peer disconnection.

The client owns a fixed 3,000 ms post-disconnect communication barrier. The GAP
callback immediately blocks new admissions and invalidates old-link data. If a
radio submission was already admitted, the full quiet interval starts when
that submission returns; otherwise it starts in the callback. The same final
admission point covers writes, RSSI, scan/cancel, connect/cancel, discovery,
subscription, initialization, and termination. No blocked operation
is replayed. In-memory callback cleanup may continue, while old-link RX frames
and results are discarded. `communicationSilenced()` and
`communicationSilenceRemainingMs()` expose read-only state so an owner can
avoid futile work but cannot shorten or bypass the barrier.

The owner consumes bounded queued RX evidence before deciding packet silence.
Only frames captured before the relevant deadline and still fresh when serviced
can refresh it; malformed, stale, or previous-generation frames cannot revive
the stream. First-packet timeout is 5 s; Bookoo's valid-packet timeout is 8 s.
Heartbeat attempts keep their protocol interval even after a recoverable write
rejection. The worker continues consuming fresh weights and checking packet
timeouts while the link remains up.

Bookoo weight/timer packets require the 20-byte `03 0B` envelope and XOR of the
first 19 bytes. Command packets use `03 0A` and XOR of the first five bytes,
including start/stop/reset and combined tare-start. Host fixtures check this
against the [manufacturer's protocol](https://github.com/BooKooCode/OpenSource/blob/main/bookoo_mini_scale/protocols.md).
These checks establish protocol conformance; the tightened parser and corrected
commands still require qualification on the actual scale model and firmware.

Command responses use a dedicated, statically allocated semaphore; general
worker wakeups cannot complete an ATT write. Submission resource errors and
completed ATT rejections preserve a usable link. Unknown errors, stale GATT
handles and unresolved one-second command timeouts still terminate it. Commands
with uncertain outcomes are never automatically replayed. GAP/reset causes and
teardown errors are recorded separately from command failures and survive
reconnection in `diagnostics()`.

Each protocol may define a minimum application-command interval. Bookoo uses
100 ms for both acknowledged and unacknowledged writes; the client services
disconnect evidence while waiting and revalidates the connection generation
at the common NimBLE admission point immediately before submission. Other
protocols keep their existing timing.
The optional log bridge emits one INFO `ble tx` line for each command accepted
by NimBLE and each subscription or initialization write attempt, with its label,
response mode, and full hexadecimal payload. A separate `command done` line
records command outcomes, connection generation, inter-command gap, raw result,
and elapsed time. A locally rejected command has only `command done` with
`submitted=0`. Peer addresses are not logged.

Classified BLE failures produce one `WARNING` `ble error` line with the
operation, status domain, NimBLE raw value (decimal and hexadecimal), low-byte
code, and firmware reason. This includes connection failures, unexpected scan
and GATT failures, recoverable command rejections, RSSI read errors, and cleanup
errors; normal discovery completion is not an error. The log covers statuses
reaching this client, not every controller-internal HCI event. A command failure
may also have its existing `INFO` `command done` outcome line.

Relaxed / Balanced / Aggressive scan presets retain their 25%, 50% and 100% duty
semantics. Fixed advertisement slots and fixed GATT handle storage avoid a
heap allocation per advertisement. Protocols that permit UUID-only discovery
still match without a GAP name; Varia and Eureka continue to require one. The
production host uses the hardware-qualified external NimBLE allocator, a 4096
byte internal host-task stack, MTU 32 and conservative fixed pools.

Connectability follows the newest advertisement from each peer. A later
nonconnectable report cannot reuse an older connectable report, while the scan
response paired with a connectable advertisement can still complete name or
service discovery. Failed connection attempts begin with short delays, then
spread out progressively to a five-second maximum; valid weight data restores
the short initial delay.

`EspressoScaleBLE` is a single-owner object: create it, call it, and destroy it
from one task only. It is intentionally non-copyable and is not thread-safe.
Call `disconnect()` before transferring BLE ownership to another component.

Useful diagnostics are available through:

- `isScanning()`
- `lastDisconnectReason()` / `lastDisconnectReasonName()`
- `lastValidPacketAgeMs()` (`UINT32_MAX` until the first valid packet)
- `rejectedPacketCount()`
- `reconnectCount()`

Every command validates connection and protocol capability internally. The
legacy `beep()` method no longer substitutes tare for sound; it now behaves
like `beepWithoutStateChange()` and succeeds only on Bookoo/generic scales.

The current `ScaleProtocol` contract parses weight and timer readings; it has
no verified physical-button tare notification. Consumers must not interpret a
weight drop to zero as proof of tare: removing an untared cup can produce the
same trace. Firmware-issued tare and its known cup reference are handled by
the controller's [tare policy](../../docs/settings/tare.md).

The single worker owner can read `getWeightSample()` after
`newWeightAvailable()`: grams, notification capture time and capture sequence
are returned from the same consumed RX frame. `notificationSequence()` reads
the latest queued notification identity to establish a command boundary;
buffered earlier frames retain their original identity/time. Sequence zero is
reserved and skipped on wrap. Native notification callbacks only copy bounded
frames into the RX ring; the bundled owner processes that ring on a 10 ms
cadence and never from an ATT command-yield path. Its final pre-tare safety
harvest remains immediately before tare. `getWeight()` remains available.
Neither write
completion nor these metadata certify unobservable physical cup motion.

Run the host lifecycle/parser suite with:

```sh
./libraries/EspressoScaleBLE/tests/run_host_tests.sh
```

The suite also executes the production NimBLE client with deterministic
platform doubles for callback ordering, worker wakeups, command errors and
invalid notifications. The portable lifecycle reducer is a separate model,
not the production client's state machine. Target scheduling and radio behavior
still require hardware qualification.
The client tests also run notification callbacks on a real thread against
power-off publication using a synchronized platform double. This TSAN coverage
is limited to that boundary. Accepted parser fixtures seed length and bit
mutations, including repaired integrity bytes, for nine known grammars; these
are parser contracts, not device captures. Legacy Acaia and MyScale framing
still need independent device evidence.

Build the bundled firmware through `./scripts/dev build`; see
[Build environment](../../docs/BUILD.md).

## Integration

This is an internal IDF component dependency, not a standalone Arduino sketch.
Build and validate the bundled application with:

```sh
./scripts/dev test ble
./scripts/dev build --arch n16r8
```

For a new consumer, follow the ownership/lifecycle rules above and inspect the
existing `OpenBrewByWeightScaleWorker.cpp` scale worker. It demonstrates
host readiness, serialized commands and connection-generation handling; calling
BLE APIs from arbitrary application callbacks is not an equivalent integration.

## Espresso Machine Compatibility

Machine wiring and stop behavior belong to the host application, not this scale
library. Use the current [machine types](../../README.md#machine-types) and
[hardware guidance](../../docs/HARDWARE.md). Historical upstream kit compatibility
does not certify an installation of this firmware.

<a id="printed-circuit-board"></a>
<a id="historical-upstream-configuration-notes"></a>
<a id="demo"></a>
<a id="scale-compatibility-1"></a>
<a id="bugsmissing"></a>

Earlier Arduino examples, kit tables and demos belong to the upstream project's
history. Legacy options such as `MOMENTARY`, `REEDSWITCH`, `AUTOTARE` and
`TIMER_ONLY` are not the configuration interface for the current application.
Use [Build](../../docs/BUILD.md) and the [settings index](../../docs/README.md#settings).

Known limits include grams-only reporting and the Pyxis auto-tare sensitivity
noted in the capability table.

## Acknowledgement
This library is maintained by Felipe Urzúa
([Cheerpipe/AcaiaArduinoBLE](https://github.com/Cheerpipe/AcaiaArduinoBLE)).
It derives from [tatemazer/AcaiaArduinoBLE](https://github.com/tatemazer/AcaiaArduinoBLE)
by Tate Mazer.

This is largely a basic port of the  [LunarGateway](https://github.com/frowin/LunarGateway/) library written for the ESP32.

In addition to some minor notes from [pyacaia](https://github.com/lucapinello/pyacaia) library written for raspberryPI.

Felicita Arc support contributions from baettigp and A-TWJ

Bookoo contributions from philgood and same31

AtomHeart Eclair protocol information from AtomHeart-Lang

Decent, DiFluid, MyScale, Varia, Eureka, and WeighMyBru protocol knowledge from
[gaggimate/esp-arduino-ble-scales](https://github.com/gaggimate/esp-arduino-ble-scales)
by [jniebuhr](https://github.com/jniebuhr) and contributors. Those protocols
were reimplemented here from that public reference; no source was copied.

lunar 2019 contributions from jniebuhr
