# Concurrency and lock order

Mutable state has one owner unless its publication is explicitly listed here.
Task-only compound snapshots use `TaskMutex` (a priority-inheriting FreeRTOS
mutex); ISR-shared scalar state uses a `portMUX`; monotonic counters and latches
use lock-free atomics. No code may perform Serial, network, flash, allocation,
formatting, a PSRAM walk, or an unbounded copy while holding a `portMUX`.

This also applies to kernel copies: FreeRTOS queue storage, queue source and
destination buffers, and the `uxTaskGetSystemState` capture array must be in
internal RAM. The persistence queue carries a one-byte token; its single
PSRAM mailbox is immutable from enqueue until the control task consumes the
worker's completion. A failed enqueue releases that ownership immediately.
The Companion object remains internal because it contains callback spinlocks.

## Lock DAG

Locks are normally acquired one at a time. The only permitted nesting is:

1. lifecycle mutex → webhook state mutex;
2. network work-buffer mutex → JSON document lifetime;
3. OTA task mutex → flash-I/O recursive mutex; and
4. NimBLE advertisement mutex → NimBLE client-state mutex.

No reverse edge is permitted. A callback that would require one must publish a
queue item or atomic latch for the owner instead.

Control status gathers machine/relay, scale telemetry, preferred-scale,
persistence and Companion inputs before taking its publication mutex. The
machine facade supplies a small scalar sample; publication does not allocate a
second full status snapshot. Readers can use the previous committed version
while an input owner is busy. The status version is published with the completed
commit, and refresh acknowledgment follows it.

The scale consumer selects the critical result or, otherwise, the timer-start
result under one acquisition of their shared mutex. It processes the copied
event after unlocking and retains both control-loop drain checkpoints.
Weight delivery uses a fixed 16-event FIFO under its existing task mutex;
overflow explicitly invalidates sample evidence instead of silently joining
nonconsecutive readings. No parsing or cup-state transition runs under that
mutex. Idle-tare claim/cancel/approved-sequence updates use the separate request
mutex; neither mutex nests with the other or spans ATT. Unvalidated publication
defers claim using the existing worker tick and unchanged command expiry.
Cup/idle-tare diagnostic scalars and worker outcome/drop snapshots are gathered
by control before taking its status publication mutex. Debug export reads the
committed copy, including uncertainty and the last terminal reason.
The NimBLE advertisement mailbox copies a raw six-byte address and bounded name
under the existing nested spinlocks; the consumer formats its private address
copy after unlocking. A concurrent advertisement remains pending for the next
consumption.

The relay `portMUX` is independent and may never nest with another lock. Its
section contains only GPIO and bounded DRAM scalar state; RTC checksum/history
publication and timer cleanup occur after interrupts are re-enabled. The debug
ring is task-only and copied linearly under `TaskMutex`; USB output is emitted
by the bounded `serial_log` queue on core 0.

## P2 spinlock inventory

This is the retained-lock inventory for the resource/concurrency qualification
work; the filename/section label is preserved for existing references.

The task-only bullseye configuration, BLE Companion publication,
settings-persistence handoff, and scale critical/weight handoffs use static
FreeRTOS mutexes. These paths can be reached from lower-priority HTTP,
network, persistence, BLE or control tasks and therefore require priority
inheritance; disabling interrupts was not justified.

The remaining `portMUX_TYPE` groups are tracked explicitly:

- relay and independent hardware-timer state: shared with an ISR; must remain
  spinlocked and be measured on target;
- local buzzer state: short task-side GPIO/timer publication, pending target
  measurement before deciding whether a mutex is safe;
- scale link/beep/debug snapshots: hot BLE/control publication, pending the
  event-driven worker conversion;
- time service, native BLE runtime, Companion NimBLE and EspressoScaleBLE
  callback registries/queues: callback-facing state whose call context must be
  proven before conversion.

No listed section may allocate, log, access flash/PSRAM-dependent data or call
a blocking API while locked. Qualification is incomplete until target tracing records
the maximum interrupts-disabled duration for every retained group.

## Snapshot contract

Control, gate, recipe, profiler, scale-link, network, OTA and webhook readers
must use their snapshot APIs. A snapshot includes a publication timestamp or
age; control status also carries a monotonic `snapshotVersion`. Observational
monotonic metrics may be atomic. Metrics used to authorize control must be part
of the same coherent snapshot as the decision state.
