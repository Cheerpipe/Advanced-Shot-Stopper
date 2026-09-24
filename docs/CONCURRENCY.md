# Concurrency and lock order

Mutable state has one owner unless its publication is explicitly listed here.
Task-only compound snapshots use `TaskMutex` (a priority-inheriting FreeRTOS
mutex); ISR-shared scalar state uses a `portMUX`; monotonic counters and latches
use lock-free atomics. No code may perform Serial, network, flash, allocation,
formatting, a PSRAM walk, or an unbounded copy while holding a `portMUX`.

This also applies to kernel copies: FreeRTOS queue storage, queue source and
destination buffers, and the `uxTaskGetSystemState` capture array must be in
internal RAM. The persistence queue carries a small work discriminator. Its
settings mailbox and PSRAM-backed shot-store image are immutable from enqueue
until control consumes the worker's generation-tagged completion. A failed
enqueue releases that ownership immediately; the dirty generation remains
pending.

## Lock DAG

Locks are normally acquired one at a time. The only permitted nesting is:

1. lifecycle mutex → webhook state mutex;
2. network work-buffer mutex → JSON document lifetime;
3. OTA task mutex → flash-I/O recursive mutex; and
4. NimBLE advertisement mutex → NimBLE client-state mutex.

No reverse edge is permitted. A callback that would require one must publish a
queue item or atomic latch for the owner instead.

Control status gathers machine/relay, scale telemetry, preferred-scale,
and persistence inputs before taking its publication mutex. The
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
mutex. Native NimBLE notification callbacks only copy bounded frames into the
client RX ring. The scale owner parses and publishes queued weight frames on a
10 ms cadence; command, policy, and beep wakeups cannot accelerate that normal
polling, and ATT-yield paths do not drain it. The safety-critical final
pre-tare harvest remains immediately before tare. The worker keeps its 1 ms
service cadence only while establishing a connection. Idle-tare
claim/cancel/approved-sequence updates use the separate request
mutex; neither mutex nests with the other or spans ATT. Unvalidated publication
defers claim using the existing worker tick and unchanged command expiry.
The final pre-write harvest precedes the idle claim. Its weight sample must
match the latest notification sequence and the control-approved published
packet; otherwise the still-queued command yields to control. The claim freezes
the capture boundary immediately before command issuance, so a buffered
pre-write zero cannot acknowledge the tare. No new mutex spans ATT.
The same request mutex protects a control-approved pre-tare sample copy. Control
publishes that copy before approving the corresponding idle claim. Immediately
before each tare, the worker copies it only if its packet/generation matches the
published stream and its capture time is fresh; no mutex spans the BLE write.
The frozen pre-write weight returns through the existing command event or durable
idle status. Control translates the anchor, or invalidates it when evidence is
unavailable; enqueue-time load changes cannot accumulate as reference error.
Cup/idle-tare diagnostic scalars and worker outcome/drop snapshots are gathered
by control before taking its status publication mutex. Debug export reads the
committed copy, including uncertainty and the last terminal reason.
Calculated cup weight and validity travel in the same `CupTareDiagnostics` copy
inside `ControlStatusSnapshot`, with the placement identity. Control alone
qualifies the FSM's stable absent/present records, calculates their difference,
and invalidates the evidence. Qualified idle unloading can reuse its trusted
empty anchor without forging a new stable-absence record. The scale worker returns
an opaque request ID, pre-write weight, and tare outcome for uncertainty handling.
Both status JSON paths publish `cupPresence`
with `weightG` (finite grams or null), `weightValid`, and `placementId` from that
committed snapshot. `cupPresence.idleTare` projects readiness from those same
diagnostics; the Web layer never reads or changes the cup FSM directly.
The NimBLE advertisement mailbox copies a raw six-byte address and bounded name
under the existing nested spinlocks; the consumer formats its private address
copy after unlocking. A concurrent advertisement remains pending for the next
consumption.

Application scale writes remain single-owner operations. Protocol metadata
sets their minimum interval (100 ms for Bookoo), and the NimBLE owner services
callbacks while waiting before rechecking ready state, disconnect evidence,
handle, and connection generation. A power-off request publishes a terminal
generation barrier under the existing scale mailbox spinlock before it can
compete with queued work. Producers then reject new commands, queued commands
complete through their stale-result path, and heartbeat/beep/debug mailboxes
cannot bypass the barrier. No scale lock spans ATT or GAP. The GAP callback
immediately marks link loss and rejects new admissions. If one radio submission
was already admitted, it finishes before the full 3,000 ms quiet interval
starts; otherwise the interval starts in the callback. Every NimBLE procedure
shares that library-owned final admission point; firmware can observe the
remaining time but cannot clear or bypass it. Pre-disconnect weight events
carry the disconnect sequence and are rejected after the epoch changes. Every
actual application-command submission and terminal outcome is emitted at INFO with
its operation, generation, response mode, spacing, raw status, and elapsed
time; payload bytes and peer addresses are excluded.

The relay and independent safety-timer `portMUX` sections are independent and
may never nest with another lock. The timer captures callback state under its
spinlock, invokes the relay callback after releasing it, and rejects stop/re-arm
while that callback is in flight. Relay sections contain only GPIO and bounded
DRAM scalar state. The local buzzer, debug ring, and other task-only compound
state use `TaskMutex`. Heap/CPU sampling and task-profiler capture belong to the
core-0 health worker; control consumes its one-slot mailbox without waiting and
ages stale samples explicitly. USB application logs are emitted by the bounded
`serial_log` queue on core 0. Eight short log records remain internal while a
bounded 2.5 KiB PSRAM buffer carries the CLI reply published to that owner;
saturation increments dropped/truncated counters instead of waiting in control.

## P2 spinlock inventory

This is the retained-lock inventory for the resource/concurrency qualification
work; the filename/section label is preserved for existing references.

The task-only bullseye configuration,
settings-persistence handoff, and scale critical/weight handoffs use static
FreeRTOS mutexes. These paths can be reached from lower-priority HTTP,
network, persistence, BLE or control tasks and therefore require priority
inheritance; disabling interrupts was not justified.

The remaining `portMUX_TYPE` groups are tracked explicitly:

- relay and independent hardware-timer state: shared with an ISR; must remain
  spinlocked and be measured on target;
- scale link/beep/debug snapshots: hot BLE/control publication, pending the
  event-driven worker conversion;
- time service, native BLE runtime, and EspressoScaleBLE
  callback registries/queues: callback-facing state whose call context must be
  proven before conversion.

No listed section may allocate, log, access flash/PSRAM-dependent data or call
a blocking API while locked. Qualification is incomplete until target tracing records
the maximum interrupts-disabled duration for every retained group.

## Snapshot contract

BBW candidate windows, learning generations and both per-preset offsets belong
to control. Network reads gain, alpha baseline, provenance, evidence count and both offset
previews from the same committed control snapshot as the active preset ID;
it never reads candidate RAM. Cycle/finalizer snapshots retain the actual applied
gain and generation. Deferred persistence still owns writes; no new task, queue,
mutex or lock-order edge is introduced.

Control, gate, recipe, profiler, scale-link, network, OTA and webhook readers
must use their snapshot APIs. A snapshot includes a publication timestamp or
age; control status also carries a monotonic `snapshotVersion`. Observational
monotonic metrics may be atomic. Metrics used to authorize control must be part
of the same coherent snapshot as the decision state.

The network task progress timestamp is a relaxed atomic because it is a
monotonic observational metric. Reset-history checkpoint and clear operations
hold the shared flash-I/O lock across the durable write and publication of the
live mirror/checkpoint timestamp, so failed writes cannot publish or resurrect
state.
