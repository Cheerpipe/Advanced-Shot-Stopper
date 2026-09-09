# ESP and FreeRTOS resource ownership

BBW prediction/learning has no resource authority. Control owns the fixed
per-preset candidate bank and immutable shot/finalizer snapshots; persistence
owns deferred durable writes. Network consumes published state only. This
adds no task, queue or handle owner and changes no lock ordering. See
[BBW policy and storage](ARCHITECTURE.md#bbw-policy-and-storage).

Every fallible resource acquisition needs one owner and a defined rollback
path. Read this before adding queues, tasks, clients, or persistent handles. `UniqueResource<Handle, Deleter>` is a one-handle-wide,
non-allocating owner with move, `release()` and `reset()`. It is used only for
C handles whose cleanup is safe in the owner's context. FreeRTOS tasks are
never deleted by this wrapper: their owners retain explicit stop/ack/join.

| Resource | Owner | Teardown/rollback |
|---|---|---|
| Dynamic CPU limits | ControlOrchestrator via `applyPowerProfile` | boot-lifetime `PowerClock`; configure only changed limits outside locks, fixed-80-MHz fallback on failure; Arduino clock setter only during boot |
| Energy activity mailbox | HTTP publishes bounded presence; control expires it and publishes applied profile/cooldown; ScaleService and NetworkService publish busy/error state | static atomics, no new tasks or flash writes; requests never authorize actuation |
| BLE modem sleep / scan and Wi-Fi sleep | ScaleService / NetworkService respectively | owner-local application, live link/connecting gates, saved preferences restored when PM is off; see [Power management](settings/power-management.md) |
| webhook `esp_http_client` | `WebhookDispatcher::httpClient_` (`UniqueResource`) | normal cleanup after worker join; destructor is final rollback |
| OTA write handle | `ShotStopperOta::otaHandle_` (`UniqueResource`) | abort under `FlashIoGuard`; `release()` transfers it exactly once to `esp_ota_end` |
| OTA SHA context | `ShotStopperOta::sessionSha256_` (`UniqueResource`) | `psa_hash_abort` then capability-aware heap free |
| Network command queue and lifecycle semaphores | `ShotStopperNetwork` (`UniqueResource`) | acquired before task creation; reset in reverse order after manager join |
| relay `esp_timer` constructor temporaries | local `TimerRollbackOwner` | automatic reverse rollback until both timers and the independent timer are ready |
| network/webhook tasks | owning service, borrowed `TaskHandle_t` | stop request, task acknowledgement, join, then queues/buffers/clients |
| scale and persistence tasks/queues | boot-lifetime owning service | startup fault rollback; resources remain stable after successful boot |
| idle scale tare status and approved pre-write sample | ScaleService; control publishes validated sample copies and accesses its request API | task mutex serializes sample copies/claim/cancel/status; never held during BLE writes; WRITING cannot be cancelled; control releases terminal/expired requests and owns anchor translation |
| ordered scale-weight handoff | ScaleService producer, control consumer | static 16-event FIFO under task mutex; overflow drops the incomplete window and marks discontinuity; no allocation or dynamic teardown |
| static task mutex/event storage | containing static object | no heap allocation and no dynamic teardown |
| HTTP server | NetworkService | manager-task-only stop/restart; handle cleared immediately after `httpd_stop` |
| mDNS responder and its service task | NetworkService | STA HTTP only with AP down; RF generation checks cancel stale startup; `mdns_free` stops it for shots, scale connection attempts, HTTP/AP/network teardown; SDK teardown may block and never runs on control |
| persistence mailbox | control producer, then persistence worker | one external request; internal token queue; producer may reuse only after consuming completion, or failed enqueue |
| webhook queue / payload | `WebhookDispatcher` | internal queue storage and external HTTP payload; release after worker join, or startup rollback |
| profiler workspace / capture | `TaskProfiler` | external processing workspace and separate internal kernel capture; free both on stop or failed start |
| cJSON document | parsing caller | PSRAM allocations through process-wide hooks installed once before HTTP starts; `cJSON_Delete` releases each independent document |

`initJsonParser()` installs the cJSON allocator once, before concurrent users
start. No caller may replace the process-wide hooks afterward. This is not a
resettable arena: simultaneous documents never share storage, and allocation
failure rejects the current parse without invalidating another document.
External allocations fail closed; they never fall back to internal RAM.

The remaining raw task and server handles are deliberate lifecycle tokens, not
unowned allocations. Converting a task handle to a destructor that invokes
`vTaskDelete` would violate the join rule and may free state while code is still
executing. The HTTP server similarly owns internal LwIP callbacks and must be
stopped on the manager task before its token is cleared.

Host evidence consists of allocation/task fault injection already in the main
harness, OTA concurrent TSAN, ASan/UBSan tests and
`resource_owner_host_test.cpp`, which checks move, release, replacement,
idempotent reset and scope rollback. Target resource counts remain part of the
combined soak gate.

The webhook worker keeps a dequeued event pending while the radio gate is
closed; stop-after-drain includes that locally held event. An empty 50 ms queue
wait is followed by the lifecycle check without an additional 25 ms delay.
The 25 ms gated wait and the gate check immediately before HTTP remain.
`webhook_worker_host_test.cpp` executes this worker with deterministic queue,
transport and heap injection, including stale configuration and cancellation.
These tests establish ordering and accounting, not target CPU or RF latency.
