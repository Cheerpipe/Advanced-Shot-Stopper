# Scheduling qualification

The authoritative task contract is `ShotStopperScheduling.h`. It fixes core,
priority, nominal period, service deadline and Task Watchdog participation for
every application task. A task without a periodic deadline is event-driven or
diagnostic and must never execute on a safety path.

## Runtime evidence

The coherent control-status snapshot publishes a monotonic version/timestamp,
the lifetime maximum control and scale-worker service gaps, monotonic deadline
miss counts, and stack high-water marks. A 10 ms deadline applies to both 1 ms
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
