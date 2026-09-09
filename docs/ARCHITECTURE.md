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

## BBW policy and storage

`ShotStopperBbwCutoff.h` dispatches fixed-size prediction/learning inputs to
independent regression and adaptive EWMA implementations. Shared scale qualification,
direct confirmation, guards, control arbitration and machine/safety authority
remain outside the policy. The original trend fit remains shared with accidental
touch sensing; only EWMA cutoff uses centered OLS. Strategies neither actuate
hardware nor write flash.

Control owns `BbwLearningBank`: up to eight identity-keyed states, under 3,000
bytes total, with 20 observations and five trajectory anchors per preset.
Four fixed gains compete against the current gain, including a custom incumbent.
Scoring replays the retained window from its anchors and is bounded
post-finalization work, with no allocation or per-sample persistence. The cycle
and pending finalizer capture preset, algorithm/profile, full-precision offset,
actual alpha and a per-learner generation. Learning checks the originating state;
reset/delete/recreation or a conflicting update invalidates it. Recipe changes
clear evidence; algorithm-only selection freezes/resumes retained EWMA state.
Editing reset bases alone preserves current learning and evidence.
Settings status publishes active-preset identity, both offsets, alpha baseline, gain/provenance
and evidence count together in the existing coherent control snapshot.

Settings V11 retains the 252-byte RuntimeConfig, 104-byte ShotPreset and
2616-byte settings blob. Runtime byte 251 and preset byte 45 hold the selector;
obsolete preset cup floats at bytes 84–91 become EWMA offset (float), alpha
(hundredths), initial/learned provenance, EWMA profile version and alpha baseline
(hundredths, byte 91; reserved zero in V9).
V1–V8 decoders verify the original checksum before explicitly initializing these
bytes. The old offset remains regression's and seeds EWMA. V9 migration adds
baseline 0.30 and EWMA profile v2 while retaining selection, offsets and gain/source.
New schemas retain saved choices and valid learned gains. Candidate anchors/observations/generations are RAM
only; deferred persistence retains offsets, gain/provenance and profile through
the existing dual-slot owner. Unknown/invalid schemas follow existing recovery;
old binaries do not understand V11, so downgrades are not settings-preserving.
V11 names RuntimeConfig byte 5 (former padding) as global
`powerManagementEnabled`. Every V1–V10 migration initializes it to false after
validating the original CRC; presets never copy it.

History V4 keeps 48-byte records and 120 entries. Guard byte bits 5–7 encode
profile (0 unknown, 1 pre-selector regression with unknown version, 2 regression v1,
3 adaptive EWMA v1, 4 EWMA v2). Alpha is 0 unknown or 1–100 hundredths;
extension bits 2–4 hold its low three bits, cut-type bits 4–7 its high four.
Extension bits 5–6 encode learning application (0 unknown,
1 skipped, 2 applied). Guard, rating, extension and weight-source meanings are
preserved. V1 migration clears newly assigned bits explicitly after CRC
validation, preserving records and offsets. Shot-type bits 2–7 and cut-type
bits 2–3 hold the captured preset ID (low six/high two bits); type/cut readers
mask the low two bits. V1/V2 migration explicitly sets unknown preset ID zero
after CRC validation; V2/V3 alpha codes become hundredths, with V3 preset IDs
and all historical policy versions retained. Preset/BBW writers preserve each
other's bit fields. V4 is rejected by
older firmware. The ID follows existing preset allocation, not a historical
name lookup or globally unique physical-device identity.
Decoding checks the supplied length before reading record CRCs and copies only
that validated length; compact inputs do not require a full-store allocation.
Profile rules are immutable: changing prediction, gain candidates or eligibility
requires versioned compatibility, not relabeling historical data. Numeric and
user contracts are in [BBW](features/brew-by-weight.md#cutoff-algorithms-and-learning)
and [shot history](features/shot-history.md).

## Residual qualification

RuntimeConfig V8 names byte 250 as the default-ON `autoTareOutsideBrew` switch,
preserving its 252-byte layout and the 2616-byte settings blob. V1–V7 migrations
initialize the former padding explicitly; V8 loads retain saved OFF. This
machine setting is not part of the per-shot/preset recipe snapshot.

Idle tare arbitration lives in `control/ShotStopperCycleRuntime.inc`, reusing
the cup FSM's PLACED event and ScaleService's TARE_ONLY transport. Worker
request lifetime and control-owned cup provenance remain separate; see
[scale commands](STATE_MACHINES.md#scale-commands-scalecommandtype--outbound).

Source boundaries do not prove real FreeRTOS interleavings or interrupt
latency. Release evidence must still include the target trace and HIL runs in
`SCHEDULABILITY.md` and `P2_RESOURCE_BUDGETS.md`.
