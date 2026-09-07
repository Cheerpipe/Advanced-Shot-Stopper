# Firmware architecture and ownership

Use this reference when changing module boundaries. For the runtime sequence,
read [State machines](STATE_MACHINES.md); for a first code change, start with
[Contributing](../CONTRIBUTING.md).

The integration roots `src/shotStopper.cpp` and `src/ShotStopperNetwork.cpp`
assemble implementation fragments in `src/control/`, `scale/`, `network/`,
`persistence/`, `diagnostics/`, and `platform/`. Fragments compile in their
owning translation unit. Shared headers carry fixed-size values, pure policy,
or explicit callback tables; each service owns its mutable state.

| Service | Owns mutable state | Accepts | Publishes |
|---|---|---|---|
| SafetyKernel | relay, safety timers, watchdog and reset history | bounded control intention | `RelaySafetySnapshot`, safety events |
| ControlOrchestrator | active session, cup/recipe policy and command arbitration | scale/machine/network messages | fixed-size control snapshots and commands |
| ScaleService | BLE central link, scale queues and radio policy | `ScaleCommand`, immutable worker policy | `ScaleEvent`, `ScaleLinkSnapshot`, RF state callback |
| NetworkService | Wi-Fi/httpd, request parsing and webhook transport | `NetworkBridgeCallbacks`, control snapshots | `WebCommand`, transport diagnostics |
| PersistenceService | NVS/EEPROM/partition serialization and write schedule | fixed-size records | success/failure result messages |
| DiagnosticsService | logs, counters, task/heap snapshots and exports | observational snapshots | JSON/serial evidence only |

## Dependency rules

1. SafetyKernel never includes or calls Wi-Fi, HTTP, JSON, webhook or network
   code. Its ISR path remains fixed-size, internal-memory-only and nonblocking.
2. ScaleService never references the `networkManager` singleton. Radio state
   crosses `ScaleWorkerBridgeCallbacks`, configured once before its task starts.
3. NetworkService requests control effects only through
   `NetworkBridgeCallbacks`; it cannot call machine/relay implementation APIs.
4. Persistence owns durable writes. Other services publish fixed-size requests
   and do not hold task/spin locks across flash I/O.
5. Diagnostics are observational. No diagnostic result authorizes the relay or
   mutates session state.
6. C task handles are borrowed lifecycle tokens: their owner must execute
   stop/ack/join. Other ESP/FreeRTOS handles use a unique non-allocating owner
   or an explicitly documented static lifetime.

`scripts/check_architecture.py` gates these rules and caps growth of the three
legacy concentration points. The caps are not quality targets: when a feature
would exceed one, extract it into its owning service rather than raising the
limit. Current independent host harnesses exercise SafetyKernel, OTA,
persistence, webhook policy, BLE protocol/runtime and shared resource owners
without including either monolithic integration root.

## Residual qualification

Source boundaries do not prove real FreeRTOS interleavings or interrupt
latency. Release evidence must still include the target trace and HIL runs in
`SCHEDULABILITY.md` and `P2_RESOURCE_BUDGETS.md`.
