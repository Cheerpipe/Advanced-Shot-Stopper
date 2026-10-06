# ESP and FreeRTOS resource ownership

BBW prediction/learning has no resource authority. Control owns the fixed
per-preset candidate bank and immutable shot/finalizer snapshots; persistence
owns deferred durable writes. Network consumes the published Home snapshot;
bounded history reads and explicit user mutations share the one static
`shotStoreMutex` with control finalization and persistence-image capture. The
`ActivationStores` component is the single explicit owner of the three
activation ring stores (stats shot log, curve sidecar, activation history).
the curve sampler, retained records and task-owned finalization/JSON workspaces
use PSRAM; flash transfers use the shared internal scratch. The curve worker
commits one reserved 8 KiB record block at a time and acknowledges physical
progress without clearing newer RAM changes. Clear epochs and delete markers
belong to this same owner; no sampling-time writer or central append index exists.
Every read, append, page query, mutation, and immutable image capture runs under
that mutex. Control skips clean record payloads and captures only uncommitted
curves with their flash progress metadata. The generalized persistence worker
writes the captured image without the mutex; control clears dirtiness only when
the completed generation is still current. Control never acquires the flash lock. See
[BBW policy and storage](ARCHITECTURE.md#bbw-policy-and-storage).

The v1 Home shot stream carries the existing card's scalar values and accepted
curve observations. Sequence/base, boot/cycle/shot identity and curve cursors
govern replacement and append updates; bind and resync share one coalesced
dispatch. Unavailable weight, average flow, first drop and tare are explicit
`null` values. Active average flow uses the control owner's fresh accepted
weight, known scale baseline and confirmed first drop. Qualifying results use
the exact Stats record; nonqualifying results preserve their firmware cutoff
time. This presentation contract adds no guard decisions or actuation messages.

The same socket carries independent Home field patches, including Equipment,
quick settings, presets, shared navigation and command readback. Each fixed
session owns 128 CRC32 field fingerprints and a clock anchor; fields have stable
ordinals and formatting at display precision. Initial/resync frames replace the
browser cache, then unchanged values produce no data frame. Disconnected scale
weight/timer are never inspected; a single null transition clears them. Serialization
and transmit reuse the existing HTTP workspace and bounded 100 ms dispatch.
The browser uses socket activity/alive control messages for liveness and power
activity, rather than a periodic Home REST request.
The bounded Home presentation projection uses size optimization and shared
typed formatting calls to keep the existing firmware growth budget; control
and safety retain the build profile's performance optimization.

ScaleService publishes the bounded friendly/raw BLE name in its link
snapshot and retains the latest successful shot-tare result under the critical
event mutex. Control copies the name into the shot and correlates tare completion
by cycle and request before capture/finalization; event loss or a new link epoch
cannot erase that diagnostic fact or rearm control on a stale connection.

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
| scale NimBLE peer procedure | EspressoScaleBLE client, called only by ScaleService | owns final admission and retains pending link cleanup until closure is confirmed; scanning/reconnection wait for cleanup and the full 3,000 ms quiet interval from GAP completion (or completion of an already-admitted submission); callbacks publish evidence, while only the owner submits deferred termination |
| Micra cloud HTTPS client | build-selected `OpenBrewByWeightMicraService` worker | boot-lifetime 8 KiB task; a lazy 6,344-byte external work buffer holds identity, tokens, authorization header, and client state, while request E/S uses one transient 16 KiB external union; HTTP/TLS library allocations are separate; connected-scale shot transitions cancel active I/O, while Disconnect, disabled observation, STA loss, and AP entry wipe/free both workspaces and destroy the client handle |
| webhook `esp_http_client` | `WebhookDispatcher::httpClient_` (`UniqueResource`) | normal cleanup after worker join; destructor is final rollback |
| OTA write handle | `OpenBrewByWeightOta::otaHandle_` (`UniqueResource`) | abort under `FlashIoGuard`; `release()` transfers it exactly once to `esp_ota_end` |
| OTA SHA context | `OpenBrewByWeightOta::sessionSha256_` (`UniqueResource`) | `psa_hash_abort` then capability-aware heap free |
| Network command queue and lifecycle semaphores | `OpenBrewByWeightNetwork` (`UniqueResource`) | acquired before task creation; reset in reverse order after manager join |
| boot heap shaper block | `OpenBrewByWeightNetwork` | one-shot 60,000-byte internal hold taken before Wi-Fi bring-up and released once the station settle floor passes and the applicable late milestones settle (first cloud query terminal, first NTP attempt terminal), expired on the first service pass at or after 60 s even if startup is failing, or freed in `stop()` after owner join; keeps bring-up allocations out of the central DRAM free run; no shaping/retry when the hold cannot be satisfied |
| boot heap capture | `BootHeapCapture`, task-only producers under one internal static `TaskMutex` | fixed payload of at most 768 bytes in PSRAM; first stage samples retained until reset, health-task sampling stops 60 s after release; `BOOT_HEAP` copies each immutable record under the mutex and prints outside it; no task, dynamic allocation, flash write or credential capture |
| relay `esp_timer` constructor temporaries | local `TimerRollbackOwner` | automatic reverse rollback until both timers and the independent timer are ready |
| network/webhook tasks | owning service, borrowed `TaskHandle_t` | stop request, task acknowledgement, join, then queues/buffers/clients |
| scale and persistence tasks/queues | boot-lifetime owning service | BLE startup failure remains TWDT-covered through a bounded 1 s native-host stop, then clears the task handle and releases callback queues only after quiescence; resources remain stable after successful boot |
| native NimBLE host task and live stack telemetry | native BLE runtime lifecycle owner | health readers retain task lifetime under a static mutex; stop joins and deletes under that mutex within its timeout budget; competing health reads use cached telemetry |
| idle scale tare status and approved pre-write sample | ScaleService; control publishes validated sample copies and accesses its request API | task mutex serializes sample copies/claim/cancel/status; never held during BLE writes; WRITING cannot be cancelled; control releases terminal/expired requests and owns anchor translation |
| ordered scale result/weight handoff | ScaleService producer, control consumer | result queues, fallback mailboxes and static 16-weight FIFO share one task mutex for publication and selection; overflow drops the incomplete weight window and marks discontinuity; no allocation or dynamic teardown |
| static task mutex/event storage | containing static object | no heap allocation and no dynamic teardown |
| HTTP server | NetworkService | manager-task-only stop/restart; handle cleared immediately after `httpd_stop` |
| Home WebSocket | existing priority-1 HTTP server; NetworkService queues at most one dispatch | two fixed session contexts allow one bound owner and one handshake; binding expires after 5 s, takeover/close/server stop release contexts; 128 field fingerprints per session deduplicate Home independently of the shot card/curve; serialization reuses the shared PSRAM workspace, sends complete within 100 ms per header/payload or close; no new task, application TX allocation or event queue |
| mDNS responder and its service task | NetworkService | network-task-only `mdns_init`/`mdns_hostname_set`/`mdns_free`; mDNS 1.13.1 places its 4096-byte task stack in internal RAM on n16r8 for core dumps, and in PSRAM on n8r4; dynamic responder memory stays in PSRAM; always-on passive responder; `mdns_free` only in `stop()` after the task join |
| crash capture and two-slot archive | ESP-IDF panic writer owns the capture during panic; boot promotion and NetworkService own normal access | the relay opens before the RTC address snapshot; normal access holds the shared flash lock, commits a historical slot only after verification, and requires Admin unlock for raw download or confirmed deletion |
| persistence mailbox | control producer, then persistence worker | fixed-capacity internal work queue plus one external settings request and one PSRAM shot-store image; generation-tagged completion prevents clearing newer dirtiness |
| reset-history durable state | existing maintenance lease and NetworkService persistence owner | control holds clear requests until the machine is configuration-safe; NetworkService writes through the shared flash lock, and control publishes completion only after success |
| shot history, curves, activation history and last-shot aggregate | `ActivationStores` RAM data layer plus the core-0 persistence worker; Network borrows only through mutex-guarded callbacks | `shotStoreMutex` covers RAM operations and immutable image capture only; no flash/network I/O spans it, and Home receives one control-published exact-ID rating/curve snapshot |
| webhook queue / payload | `WebhookDispatcher` | internal queue storage and external HTTP payload; release after worker join, or startup rollback |
| Micra WebSocket/STOMP | cloud worker owns lifecycle; SDK receive task owns framing between callbacks | one reusable PSRAM workspace capped at 24 KiB (header 1024 B decoded in place, body 16384 B, retained frame descriptor and 60 fixed rate bins); 1024 B RX and 1024 B TX library buffers use ordinary heap, separate 6144 B internal SDK stack on core 0 priority 1; no SDK auto-reconnect; stop joins callbacks before destroy/free |
| Micra-profile TLS state | mbedTLS / owning HTTPS/WSS client | certificate, handshake, record, and session allocations use PSRAM only and are released by mbedTLS; no internal fallback |
| profiler workspace / capture | core-0 health worker via `TaskProfiler` | control/HTTP publish requests only; external processing workspace and separate internal kernel capture are freed on stop or failed start |
| scale profile capture/store | core-0 health worker via `ScaleProfiler` | owns the extended capture clock, constant-memory capacity ETA and full-buffer completion; status readers only copy estimates; scale worker and control loop append fixed-size records through one leaf capture mutex after releasing their own locks; the settings_persist worker performs invalidate/save steps against the frozen immutable generation; a download lease pins it against Start/Delete |
| USB application output | core-0 `serial_log` task | the eight-record ESP-log queue stays internal for cache-off logging; the bounded 2.5 KiB CLI reply lives in PSRAM and transfers without copying or waiting |
| cJSON document | parsing caller | PSRAM allocations through process-wide hooks installed once before BLE workers and HTTP start; `cJSON_Delete` releases each independent document |

Micra cancellation stays latched throughout the active cloud operation, including
session renewal. Network recovery and session cleanup cannot clear it. Only the
idle cloud worker consumes it before selecting the next operation; once observed,
cancellation ends HTTP progress immediately, including an `EAGAIN` result. Client
cleanup remains on that worker after HTTP progress returns. Connected-scale
shot pauses destroy the WS client and TLS state but may retain bounded application
scratch and session tokens. API selection, observation disablement, account
removal/change, STA loss/AP entry and maintenance release WS scratch after
callback quiescence. Maintenance also releases the HTTP workspace. Each
client records actual pause completion; residual SDK DNS resolution is excluded.
Control owns the admitted-shot signal and clears it immediately on rinse
classification. The scale worker owns live connection publication; Micra
transport pauses require both signals, independently of relay-critical
webhook/NTP admission. Unscaled shots, wake, backflush and rejected starts do
not cancel Micra observation. Scale acquisition does not cancel existing WSS.
The integration owns the fixed eight-transition backflush handoff under its
mutex; control is its only consumer. Diagnostics copies without draining it.
The relay owner retains both deadlines and the original close timestamp;
neither the network worker nor an SDK callback can actuate GPIO.

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

The claimed Web UI allows two concurrent read requests only for Stats or
History (small Home navigation metadata plus that page's records). Home live
updates use its session-owned socket without periodic REST polling. Other API requests remain
exclusive, including commands and OTA. Four HTTP sockets and a four-connection
backlog accommodate the two reads plus lazy HTML and JavaScript downloads.
Handlers execute serially on the existing HTTP task, retaining one owner of
the shared response workspace; overlapping requests do not add worker tasks
or concurrent workspace access.

Host evidence consists of allocation/task fault injection already in the main
harness, OTA concurrent TSAN, ASan/UBSan tests and
`resource_owner_host_test.cpp`, which checks move, release, replacement,
idempotent reset and scope rollback. Target resource counts remain part of the
combined soak gate.

The webhook worker keeps a dequeued event pending while the configured shot
gate is closed; stop-after-drain includes that locally held event. An empty 50
ms queue wait is followed by the lifecycle check without an additional 25 ms
delay. The 25 ms gated wait and the gate check before starting delivery remain;
delivery checks admission between asynchronous progress calls and after the
final call. The absolute 1800 ms deadline also rejects a late successful return.
Interrupted, possibly dispatched POSTs are dropped without replay; unsent
canceled events remain pending. `webhook_worker_host_test.cpp`
executes this worker with deterministic queue, transport and heap injection,
including stale configuration and an active shot beginning during delivery.
These tests establish ordering and accounting, not target CPU or RF latency.
