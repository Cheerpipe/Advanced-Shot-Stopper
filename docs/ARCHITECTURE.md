# Firmware architecture and ownership

Use this reference when changing module boundaries. For the runtime sequence,
read [State machines](STATE_MACHINES.md); for a first code change, start with
[Contributing](../CONTRIBUTING.md).

The integration roots `src/openBrewByWeight.cpp` and `src/OpenBrewByWeightNetwork.cpp`
assemble implementation fragments in `src/control/`, `scale/`, `network/`,
`persistence/`, `diagnostics/`, and `platform/`. Fragments compile in their
owning translation unit. Shared headers carry fixed-size values, pure policy,
or explicit callback tables; each service owns its mutable state.

| Service | Owns mutable state | Accepts | Publishes |
|---|---|---|---|
| SafetyKernel | relay, safety timers, watchdog and reset history | bounded control intention | `RelaySafetySnapshot`, safety events |
| ControlOrchestrator | active session, cup/recipe policy and command arbitration | scale/machine/network messages | fixed-size control snapshots and commands |
| ScaleService | BLE central link, scale queues and radio policy | `ScaleCommand`, immutable worker policy | `ScaleEvent`, `ScaleLinkSnapshot`, RF state callback |
| SelectedMachineIntegration | only the concrete machine's digital integration state; absent integrations use a no-op adapter | common lifecycle/config publication plus that machine's own feature requests | that machine's own bounded status types; no universal feature model |
| NetworkService | Wi-Fi/httpd/mDNS, request parsing and webhook transport | `NetworkBridgeCallbacks`, control snapshots | `WebCommand`, transport diagnostics |
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
7. Common machine-integration code exposes only boot, configuration-publication,
   worker-service and timing hooks. Capabilities, requests, status, protocol and
   policy types belong to the selected machine module; another machine is not
   required to implement or understand them. CMake compiles exactly one concrete
   adapter and only its private component dependencies.

`scripts/check_architecture.py` gates these rules and caps growth of the three
legacy concentration points. The caps are not quality targets: when a feature
would exceed one, extract it into its owning service rather than raising the
limit. Current independent host harnesses exercise SafetyKernel, OTA,
persistence, webhook policy, BLE protocol/runtime and shared resource owners
without including either monolithic integration root.

## Machine integration modules

The machine profile declares an allow-listed `integration` value explicitly;
brand, model, ID and display text never select executable code. Profiles with
`integration: none` compile the no-op adapter and do not link a concrete machine
protocol component. Linea Micra builds compile its adapter, service, bounded
feature types and HTTPS cloud client. Future machines may expose entirely
different feature APIs while retaining only the small lifecycle boundary needed
by boot, network-state publication, and generic physical-start disposition.

The Micra adapter owns a dedicated low-priority worker, bounded PSRAM response
workspace, and a PSRAM-only mbedTLS allocation profile so transient handshakes
do not fragment internal DRAM. It registers an installation key, signs La
Marzocco cloud requests, keeps short-lived access and refresh tokens in RAM,
lists account machines, and reads only the selected serial's dashboard. The
network service publishes STA, AP, and shot state through the common lifecycle
boundary. The worker never starts cloud work without STA, cancels it when AP
starts or STA is lost, and pauses state observations during shots. It has no
NimBLE dependency and cannot delay scale discovery, scale commands, or weight
delivery.

The Micra service also owns power-state freshness and the optimistic ON/OFF
overlay lifetime.
Its adapter exposes only `NORMAL` or `WAKE_PASSTHROUGH`; Open Brew by Weight owns relay
passthrough and consumes wake gestures before brew, rinse, guards, scale,
alerts, webhooks, and history. Those subsystems never depend on Micra types.

The schema-1 settings blob retains the exact 310-byte
`LineaMicraPersistedSettings` cloud account record and the two-byte per-preset
Micra target in every profile so switching a build profile cannot reinterpret
the persistence layout. Their names, validation and helpers remain Micra-owned;
unrelated machine modules must not reuse them. Settings persistence accepts only
the current magic, schema, size, checksum, and semantic contract. Every earlier
settings schema is rejected rather than migrated; this cutover requires a clean
`--erase-all` installation.

## BBW policy and storage

`OpenBrewByWeightBbwCutoff.h` dispatches fixed-size prediction/learning inputs to
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

Settings schema 1 uses the current 252-byte `RuntimeConfig`, 104-byte
`ShotPreset`, and 2,960-byte settings blob. Candidate
anchors/observations/generations are RAM only; deferred persistence retains
offsets, gain/provenance, and profile through the existing dual-slot owner.
`powerManagementEnabled`, webhook preset delivery, and Allow rinse while Armed
are current explicit fields. Presets never copy the global power setting.
Earlier settings lengths or schema numbers are rejected before field access;
there is no settings decoder or migration fallback.
The shot log is the authority for a completed shot: confirmed, non-rinse,
strictly more than 12,000 ms of brewing and a finite settled yield strictly
above 2 g. The fixed recording threshold does not alter the configurable BBW
protection window or actuation. Home and the integration snapshot resolve the
newest eligible log record by ID under `shotStoreMutex`; the curve sidecar and
rating use that same ID. Deleting the newest record advances Home to the next
eligible row, and clearing the log empties the idle card. The in-progress card
continues to use the live control snapshot. The legacy `LastShotStore` blob
remains in the binary layout for internal completed-cycle diagnostics, but its
separate good-shot field is not a public shot authority. An erase-all install
starts both histories empty; no upgrade migration is required.

History schema 1 keeps the current fixed records and entries. Guard byte bits 5–7 encode
profile (0 unknown, 1 pre-selector regression with unknown version, 2 regression v1,
3 adaptive EWMA v1, 4 EWMA v2). Alpha is 0 unknown or 1–100 hundredths;
extension bits 2–4 hold its low three bits, cut-type bits 4–7 its high four.
Extension bits 5–6 encode learning application (0 unknown,
1 skipped, 2 applied). Guard, rating, extension and weight-source meanings are
preserved in the current layout. Shot-type bits 2–7 and cut-type
bits 2–3 hold the captured preset ID (low six/high two bits); type/cut readers
mask the low two bits. Preset/BBW writers preserve each other's bit fields.
Any non-v1 history store is discarded instead of decoded. The ID follows
existing preset allocation, while the stored name is historical data rather
than a lookup through the current preset bank.
The fixed persisted stats trailer remains part of the store checksum, but
public Stats values are derived under the store mutex from the newest ten
eligible records in RAM. The read visits at most 100 ring entries and copies
at most ten records. It counts available flow separately and computes absolute
percentage error only for normal BBW target or legacy prediction cuts.

The stats shot log, its curve sidecar, and the independent activation history
are owned by one RAM data layer (`ActivationStores`) whose every access runs
under the single `shotStoreMutex`. Control and HTTP mutate only RAM and advance
a generation. The core-0 persistence worker copies an immutable image under
that mutex, releases it before flash I/O, and clears live dirtiness only when
the completion generation still matches. Acknowledgement carries the image's
flash generation and active slot back to each live store even when newer RAM
edits remain dirty, so the next snapshot advances from that flash generation.
Clearing a store preserves its generation; an older slot must never outrank
the saved empty store. Each inactive partition slot is
erased one 4 KiB sector at a time, programmed one 1 KiB staged chunk at a time,
and receives its validity-bearing header last; the worker rechecks the current
machine and scale gates between steps. The shot log's whole 7,228-byte store and the
16,024-byte activation-history store live in PSRAM and move to and from their
slots in 1 KiB chunks staged through the small internal flash-I/O scratch only
while that owner holds the flash lock. Each keeps two slots in its own data
partition — 2×12 KiB for `shotlog`, 2×16 KiB for `history` — with generation
and checksum selection preserving the atomic whole-store update; a failed
write never erases the last-good slot. Writes remain deferred until the shot
has ended. The shot log uses schema 1 in its dedicated partition; any other
schema is discarded and the stats log starts empty.

On n16r8, ESP-IDF writes one ELF core dump to a 640 KiB capture partition
after a panic. Once the relay is open and the durable boot ID has been saved,
boot code validates that image and promotes it into one of two 704 KiB slots
in the crash-history partition. Each slot commits a checksum-protected metadata
sector last, after the dump copy and SHA-256 verification. The copy replaces
the oldest valid slot only when both slots are occupied; interruption before
commit leaves the capture available for retry. The capture's first sector is
erased only after commit, so a reboot between those operations is recognized
by the boot ID and dump digest. HTTP download reads bounded chunks under the
shared flash lock and releases the lock before sending each chunk. The raw
archive requires Admin unlock because task stacks may contain secrets.

The separate shot-curve sidecar uses schema 1:
up to 121 centigram weights on a fixed half-second grid plus exact event/end
vertices for each of the same 100 eligible history records. Its 26,820-byte
whole store lives in PSRAM and moves to and from its slots in 1 KiB chunks
staged through the small internal flash-I/O scratch only while that owner
holds the flash lock. Two
28 KiB slots fill the dedicated 56 KiB `shotcurve` data partition; generation
and checksum selection preserve the existing atomic whole-store update.
Writes remain deferred until the shot has ended and do not add a transaction
for derived flow. Any other curve store is discarded. The
shot-log record's existing metric prefix and average-flow field are unchanged.

Decoding checks the supplied length before reading record CRCs and copies only
that validated length; compact inputs do not require a full-store allocation.
Profile rules are immutable: changing prediction, gain candidates or eligibility
requires versioned compatibility, not relabeling historical data. Numeric and
user contracts are in [BBW](features/brew-by-weight.md#cutoff-algorithms-and-learning)
and [shot history](features/shot-history.md).

## Residual qualification

`RuntimeConfig` retains a 252-byte fixed layout inside the current schema-1
settings blob. `autoTareOutsideBrew` remains a global machine setting rather
than part of the per-shot/preset recipe snapshot. No historical settings layout
is interpreted at boot. The optional idle accessory retare uses spare bit 6 of
the already-packed `noScaleBbwMode` byte. New and factory-reset records default
on; existing saved records retain their stored bit, including OFF. The bit is
preserved when the no-scale mode changes; blob size and schema stay unchanged.

Idle tare arbitration lives in `control/OpenBrewByWeightCycleRuntime.inc`, reusing
the cup FSM's PLACED event and ScaleService's TARE_ONLY transport. Worker
request lifetime and control-owned cup provenance remain separate; see
[scale commands](STATE_MACHINES.md#scale-commands-scalecommandtype--outbound).

Source boundaries do not prove real FreeRTOS interleavings or interrupt
latency. Release evidence must still include the target trace and HIL runs in
`SCHEDULABILITY.md` and `P2_RESOURCE_BUDGETS.md`.
