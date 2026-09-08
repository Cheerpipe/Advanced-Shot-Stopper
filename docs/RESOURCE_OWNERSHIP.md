# ESP and FreeRTOS resource ownership

Every fallible resource acquisition needs one owner and a defined rollback
path. Read this before adding queues, tasks, clients, or persistent handles. `UniqueResource<Handle, Deleter>` is a one-handle-wide,
non-allocating owner with move, `release()` and `reset()`. It is used only for
C handles whose cleanup is safe in the owner's context. FreeRTOS tasks are
never deleted by this wrapper: their owners retain explicit stop/ack/join.

| Resource | Owner | Teardown/rollback |
|---|---|---|
| webhook `esp_http_client` | `WebhookDispatcher::httpClient_` (`UniqueResource`) | normal cleanup after worker join; destructor is final rollback |
| OTA write handle | `ShotStopperOta::otaHandle_` (`UniqueResource`) | abort under `FlashIoGuard`; `release()` transfers it exactly once to `esp_ota_end` |
| OTA SHA context | `ShotStopperOta::sessionSha256_` (`UniqueResource`) | `mbedtls_sha256_free` then capability-aware heap free |
| Network command queue and lifecycle semaphores | `ShotStopperNetwork` (`UniqueResource`) | acquired before task creation; reset in reverse order after manager join |
| relay `esp_timer` constructor temporaries | local `TimerRollbackOwner` | automatic reverse rollback until both timers and the independent timer are ready |
| network/webhook tasks | owning service, borrowed `TaskHandle_t` | stop request, task acknowledgement, join, then queues/buffers/clients |
| scale and persistence tasks/queues | boot-lifetime owning service | startup fault rollback; resources remain stable after successful boot |
| idle scale tare status | ScaleService; control accesses its request API | task mutex serializes claim/cancel/status; never held during BLE writes; WRITING cannot be cancelled; control releases terminal/expired requests |
| static task mutex/event storage | containing static object | no heap allocation and no dynamic teardown |
| HTTP server | NetworkService | manager-task-only stop/restart; handle cleared immediately after `httpd_stop` |
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
