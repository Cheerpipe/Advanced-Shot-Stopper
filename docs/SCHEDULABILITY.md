# Scheduling qualification

The authoritative task contract is `ShotStopperScheduling.h`. It fixes core,
priority, nominal period, service deadline and Task Watchdog participation for
every application task. A task without a periodic deadline is event-driven or
diagnostic and must never execute on a safety path.

## Application task contract

`max block` is the largest explicit API timeout reachable in one activation;
it is not a measured WCET. `unbounded` is intentional for the isolated serial
sink: USB backpressure may block it forever, so it is neither watchdog
subscribed nor part of control. Stack values are configured bytes in ESP-IDF.

| Task | Activation / period | Deadline | Execution budget | Priority | Max block | Stack | Core | TWDT |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| control | periodic / 1 ms active | 10 ms | 9000 us | idle+1 | 50 ms maintenance flash take | 8192 | 1 | 5 s |
| scale_worker | periodic / 1 ms linked, 10 ms idle | 10 ms | 9000 us | idle+1 | 3000 ms GATT step | 6656 | 1 | 5 s |
| settings_persist | event-driven | 1000 ms service | n/a | idle+1 | 5000 ms flash take | 4096 | 1 | 5 s |
| network_manager | periodic / 50 ms | 250 ms | 200000 us | idle+1 | 2500 ms lifecycle/cancel | 10240 | 0 | 5 s |
| httpd | framework event | n/a | n/a | idle+1 | 30000 ms OTA receive budget | 8192 | 0 | no |
| webhook | event-driven | n/a | n/a | idle | 1800 ms HTTP | 4096 | 0 | no |
| serial_log | event-driven | n/a | n/a | idle | unbounded USB sink | 3072 | 0 | no |

The 10 ms service deadline does not apply while `scale_worker` is executing an
explicit connection/discovery operation whose bounded step is listed above;
those paths are separately bounded by the 5 s TWDT. HIL qualification must
report active-link and connect/discovery distributions separately.

The scale worker blocks on a task notification with the state-dependent 1 ms
linked/connecting or 10 ms idle timeout. Commands, policy changes, Companion
requests and sound mailboxes notify it immediately. The timeout remains the
compatibility path for NimBLE frames and GAP/GATT state until the backend
publishes its asynchronous wake edge; no protocol timeout depends solely on a
notification.

## Runtime evidence

The coherent control-status snapshot publishes a monotonic version/timestamp,
the lifetime maximum control and scale-worker service gaps, monotonic deadline
miss counts, maximum observed loop-body execution in microseconds, and stack
high-water marks. A 10 ms deadline applies to both 1 ms
loops. Settings persistence runs at `idle + 1`, blocks on its queue and yields
around flash work, so it cannot be indefinitely starved while subscribed to the
Task Watchdog. Network and scale metrics are published under their owning
snapshot or as monotonic atomics.

## Release test

Run the target at the qualified 80 MHz configuration for at least eight hours
with all of the following active together: BLE scan/connect/notifications,
Wi-Fi scan and reconnect, Web UI polling, webhook flood, repeated settings
writes, a full OTA with interruption at every checkpoint, and USB CDC attached
but not drained. Record the status snapshot once per second.

A build passes only when:

- there is no Task/Interrupt Watchdog reset;
- control and scale deadline-miss counters remain zero;
- lifetime service gaps stay at or below 10 ms;
- no stack watermark falls below 384 words;
- safety timers still open the circuit at their configured deadline; and
- diagnostic queue drops are monotonic/accounted and never affect control.

Keep the raw trace, firmware build ID, board revision and RF environment with
the release evidence. Any task/core/priority/clock change invalidates the
measurement and requires the test again.

An observed maximum is not a proven worst-case execution time (WCET).
Qualification requires a combined target run demonstrating each periodic
execution budget with margin and a reviewed result tied to the immutable build
ID. Record evidence for the current build rather than inferring it from a
historical development milestone.
