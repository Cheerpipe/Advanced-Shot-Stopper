# ESP and FreeRTOS resource ownership

BBW prediction/learning has no resource authority. Control owns the fixed
per-preset candidate bank and immutable shot/finalizer snapshots; persistence
owns deferred durable writes. Network consumes the published Home snapshot;
bounded history reads and explicit user mutations share the one static
`shotStoreMutex` with control finalization and persistence-image capture. The
`ActivationStores` component is the single explicit owner of the three
activation ring stores (stats shot log, curve sidecar, activation history):
every read, append, page query, mutation, and immutable image capture runs under
that mutex. The generalized persistence worker writes the captured image after
releasing the mutex and acknowledges it only when its generation is still
current; control never acquires the flash lock. See
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
| scale NimBLE peer procedure | EspressoScaleBLE client, called only by ScaleService | owns final admission for scanning, connection, discovery, commands, RSSI, and teardown; its GAP callback starts the fixed 1,000 ms disconnect barrier before notifying firmware, and no caller can shorten or bypass it |
| Micra cloud HTTPS client | build-selected `OpenBrewByWeightMicraService` worker | boot-lifetime 8 KiB task; lazy external session state is 4,120 bytes and request E/S is one transient 16 KiB union; shot transitions cancel active I/O, while Disconnect, disabled observation, STA loss, and AP entry wipe/free both workspaces and destroy the client handle |
| webhook `esp_http_client` | `WebhookDispatcher::httpClient_` (`UniqueResource`) | normal cleanup after worker join; destructor is final rollback |
| OTA write handle | `OpenBrewByWeightOta::otaHandle_` (`UniqueResource`) | abort under `FlashIoGuard`; `release()` transfers it exactly once to `esp_ota_end` |
| OTA SHA context | `OpenBrewByWeightOta::sessionSha256_` (`UniqueResource`) | `psa_hash_abort` then capability-aware heap free |
| Network command queue and lifecycle semaphores | `OpenBrewByWeightNetwork` (`UniqueResource`) | acquired before task creation; reset in reverse order after manager join |
| relay `esp_timer` constructor temporaries | local `TimerRollbackOwner` | automatic reverse rollback until both timers and the independent timer are ready |
| network/webhook tasks | owning service, borrowed `TaskHandle_t` | stop request, task acknowledgement, join, then queues/buffers/clients |
| scale and persistence tasks/queues | boot-lifetime owning service | BLE startup failure remains TWDT-covered through a bounded 1 s native-host stop, then clears the task handle and releases callback queues only after quiescence; resources remain stable after successful boot |
| idle scale tare status and approved pre-write sample | ScaleService; control publishes validated sample copies and accesses its request API | task mutex serializes sample copies/claim/cancel/status; never held during BLE writes; WRITING cannot be cancelled; control releases terminal/expired requests and owns anchor translation |
| ordered scale-weight handoff | ScaleService producer, control consumer | static 16-event FIFO under task mutex; overflow drops the incomplete window and marks discontinuity; no allocation or dynamic teardown |
| static task mutex/event storage | containing static object | no heap allocation and no dynamic teardown |
| HTTP server | NetworkService | manager-task-only stop/restart; handle cleared immediately after `httpd_stop` |
| mDNS responder and its service task | NetworkService | network-task-only `mdns_init`/`mdns_hostname_set`/`mdns_free`; mDNS 1.13.1 allocates the 4096-byte task stack and dynamic responder memory in PSRAM while static synchronization/control storage remains internal; always-on passive responder (no gating for shots, scale, AP or HTTP); `mdns_free` only in `stop()` after the task join |
| persistence mailbox | control producer, then persistence worker | fixed-capacity internal work queue plus one external settings request and one PSRAM shot-store image; generation-tagged completion prevents clearing newer dirtiness |
| reset-history durable state | existing maintenance lease and NetworkService persistence owner | control holds clear requests until the machine is configuration-safe; NetworkService writes through the shared flash lock, and control publishes completion only after success |
| shot history, curves, activation history and last-shot aggregate | `ActivationStores` RAM data layer plus the core-0 persistence worker; Network borrows only through mutex-guarded callbacks | `shotStoreMutex` covers RAM operations and immutable image capture only; no flash/network I/O spans it, and Home receives one control-published exact-ID rating/curve snapshot |
| webhook queue / payload | `WebhookDispatcher` | internal queue storage and external HTTP payload; release after worker join, or startup rollback |
| Micra-profile TLS state | mbedTLS / owning HTTPS client | certificate, handshake, record, and session allocations use PSRAM only and are released by mbedTLS; no internal fallback |
| profiler workspace / capture | core-0 health worker via `TaskProfiler` | control/HTTP publish requests only; external processing workspace and separate internal kernel capture are freed on stop or failed start |
| USB application output | core-0 `serial_log` task | the eight-record ESP-log queue stays internal for cache-off logging; the bounded 2.5 KiB CLI reply lives in PSRAM and transfers without copying or waiting |
| cJSON document | parsing caller | PSRAM allocations through process-wide hooks installed once before BLE workers and HTTP start; `cJSON_Delete` releases each independent document |

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

The webhook worker keeps a dequeued event pending while the configured shot
gate is closed; stop-after-drain includes that locally held event. An empty 50
ms queue wait is followed by the lifecycle check without an additional 25 ms
delay. The 25 ms gated wait and the gate check before starting delivery remain;
once delivery starts, it runs to completion. `webhook_worker_host_test.cpp`
executes this worker with deterministic queue, transport and heap injection,
including stale configuration and an active shot beginning during delivery.
These tests establish ordering and accounting, not target CPU or RF latency.
