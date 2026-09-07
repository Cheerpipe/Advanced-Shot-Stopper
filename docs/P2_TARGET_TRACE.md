# Target trace qualification

This is the target-only acceptance contract for scheduling and concurrency.
The P2 filename is retained for compatibility with existing evidence links. Host sanitizers
and source analysis find many ownership/interleaving defects, but cannot prove
ESP32 interrupt latency, radio-task scheduling or allocator behavior.

## Required build and capture identity

Record firmware build ID, git revision, board/PSRAM architecture, board
revision, ESP-IDF version, compile flags, CPU frequency, trace tool/version and
the exact scenario timeline. Preserve the raw trace beside the P2 soak JSONL
and summary; derived screenshots alone are insufficient.

Use ESP-IDF application-level tracing, SystemView, or an equivalent timestamped
FreeRTOS trace. Trace buffers must live in internal RAM only when the selected
backend requires DMA/cache-off access; otherwise reserve PSRAM explicitly and
show that the trace itself does not breach the heap budgets.

## Scenarios and gates

| Scenario | Minimum window | Required evidence | Failure gate |
|---|---:|---|---|
| control + scale notification flood | 30 min | task switches, control/scale execution and queue high-water | deadline miss, lost critical event, or unbounded queue growth |
| Wi-Fi reconnect + webhook HTTP failures | 8 h | task/handle count, HTTP create/reuse/cleanup, internal free/largest block | more than one live client, repeated worker creation, or sustained largest-block decline |
| interrupted/resumed OTA + NVS writes | every checkpoint | flash lock, cache-off interval, OTA owner lifecycle and journal writes | overlapping flash owners, leaked OTA handle, or write budget exceeded |
| combined BLE/Wi-Fi/webhook/OTA | 8 h qualification, 72 h endurance | raw scheduler trace plus `p2_soak.py` artifacts | any watchdog, reboot, stale snapshot, deadline miss or resource trend violation |

Every retained spinlock must be identifiable in the trace or paired with GPIO
edge instrumentation. Report count, maximum and p99 interrupts-disabled time
per lock group. The relay ISR group remains exempt from mutex conversion, not
from measurement.

Suppressions require an owner, a reason and a review/expiry date. A target race
or warning may be suppressed only with a linked artifact and written argument
showing why it cannot affect control or diagnostic integrity.
