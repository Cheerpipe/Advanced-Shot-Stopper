# Concurrency and lock order

Mutable state has one owner unless its publication is explicitly listed here.
Task-only compound snapshots use `TaskMutex` (a priority-inheriting FreeRTOS
mutex); ISR-shared scalar state uses a `portMUX`; monotonic counters and latches
use lock-free atomics. No code may perform Serial, network, flash, allocation,
formatting, a PSRAM walk, or an unbounded copy while holding a `portMUX`.

## Lock DAG

Locks are normally acquired one at a time. The only permitted nesting is:

1. lifecycle mutex → webhook state mutex;
2. network work-buffer mutex → JSON document lifetime;
3. OTA task mutex → flash-I/O recursive mutex; and
4. NimBLE advertisement mutex → NimBLE client-state mutex.

No reverse edge is permitted. A callback that would require one must publish a
queue item or atomic latch for the owner instead.

The relay `portMUX` is independent and may never nest with another lock. Its
section contains only GPIO and bounded DRAM scalar state; RTC checksum/history
publication and timer cleanup occur after interrupts are re-enabled. The debug
ring is task-only and copied linearly under `TaskMutex`; USB output is emitted
by the bounded `serial_log` queue on core 0.

## Snapshot contract

Control, gate, recipe, profiler, scale-link, network, OTA and webhook readers
must use their snapshot APIs. A snapshot includes a publication timestamp or
age; control status also carries a monotonic `snapshotVersion`. Observational
monotonic metrics may be atomic. Metrics used to authorize control must be part
of the same coherent snapshot as the decision state.
