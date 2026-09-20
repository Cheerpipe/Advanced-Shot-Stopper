/*
  Shot Stopper for La Marzocco Micra

  An activator on ACTIVATOR_GPIO (paddle, switch, or another compatible
  mechanism) reports user intention. The stopper is the sole controller of the
  machine circuit (the intercepted brew-switch contact that makes the machine
  run) through a normally-open relay.

  Activator ON  (control closed) -> GPIO LOW
  Activator OFF (control open)   -> GPIO HIGH
  Relay de-energized             -> machine circuit open (safe state)

  LAYER: Shot stopper (orchestrator)
  ----------------------------------
  This translation unit wires brew, cup, scale, and the generic machine façade.
  It must keep responsibilities decoupled:

  - The activator reads GPIO and translates it to UserIntent / MachineIntention.
    The stopper talks to Machine only through that abstract façade
    (machinePollIntention, machineRequestStart/Stop, machineRunState,
    MachineSense, machineSetActivatorDriveAllowed). It must NOT include
    paddle/momentary specialization headers, branch on MachineType, or know
    reed/pulse/PaddleMode internals.
  - Paddle logic stays in ShotStopperMachinePaddle*; momentary in
    ShotStopperMachineMomentary*. Run state is owned only by each type's
    *State* file. Brew / cup / scale / guards likewise never talk to switch
    or paddle details — if a guard uses paddle- or momentary-specific code,
    that is a layer bug.
  - Brew, cup, and scale do not call each other; this file polls machine once
    per loop, builds GuardInputs, pushes MachineSense and activator-drive
    permission, and applies scale/cup effects. Guards never call machine.

  Author: Felipe Urzúa <cheerpipe@gmail.com>
  https://github.com/Cheerpipe/AcaiaArduinoBLE
  Released under the GNU Affero General Public License v3.0.
*/

#if !defined(SHOT_STOPPER_HOST_TEST)
#ifdef LOG_LOCAL_LEVEL
#undef LOG_LOCAL_LEVEL
#endif
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include <esp_log.h>
#endif

#include "ShotStopperPowerManagement.h"

#if defined(SHOT_STOPPER_HOST_TEST)
#include "tests/shot_stopper_host_stubs.h"
#else
#include <EspressoScaleBLE.h>
#include "ShotStopperBleRuntime.h"
#include <EEPROM.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>
#include <cmath>
#include "ShotStopperNetwork.h"
#include "ShotStopperOta.h"
#include "ShotStopperPersistence.h"
#include "ShotStopperDurableStores.h"
#include "ShotStopperRecovery.h"
#endif

#include "ShotStopperDomain.h"
#include "machine/ShotStopperMachineIntegration.h"
#include "ShotStopperIntegrationState.h"
#include "ShotStopperDebugExport.h"
#if !defined(SHOT_STOPPER_HOST_TEST)
#include "ShotStopperBleScanPersistence.h"
#endif
#include "ShotStopperBuzzer.h"
#include "ShotStopperAlert.h"
#include "ShotStopperAlertChannel.h"
#include "ShotStopperAlertTone.h"
#include "ShotStopperPresets.h"
#include "ShotStopperBbwLearning.h"
#if !defined(SHOT_STOPPER_HOST_TEST)
namespace shotstopper {
Print &serialCliOutput();
}
#ifdef Serial
#undef Serial
#endif
#define Serial serialCliOutput()
#endif
#include "ShotStopperSerialCli.h"
#if !defined(SHOT_STOPPER_HOST_TEST)
#undef Serial
#if ARDUINO_USB_CDC_ON_BOOT
#if ARDUINO_USB_MODE
#define Serial HWCDCSerial
#else
#define Serial USBSerial
#endif
#else
#define Serial Serial0
#endif
#endif
#if defined(SHOT_STOPPER_USB_CONSOLE_OWN_HWCDC)
HWCDC shotStopperUsbConsole;
#endif
#include <ShotStopperVersion.h>
#include "ShotStopperHardwareTimer.h"
#include "ShotStopperResetGuard.h"
#include "ShotStopperRecoveryGesture.h"
#include "ShotStopperResetHistoryStore.h"
#include "ShotStopperSafety.h"
#include "ShotStopperActivationStores.h"
#include "ShotStopperShotLog.h"
#include "ShotStopperShotCurve.h"
#include "ShotStopperLastShot.h"
#include "ShotStopperTime.h"
#include "ShotStopperWatchdog.h"
#include "ShotStopperHwmon.h"
#include "ShotStopperTaskProfiler.h"
#include "ShotStopperTaskMutex.h"
#include "ShotStopperScheduling.h"
#include "ShotStopperPsram.h"
#ifndef SHOT_STOPPER_HOST_TEST
#include "ShotStopperJsonArena.h"
#endif
#include "ShotStopperHardware.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <new>
#include <stdlib.h>

using namespace shotstopper;

#include "ShotStopperScaleWorker.h"
#include "ShotStopperRfCoex.h"

// ---------------------------------------------------------------------------
// User configuration
// ---------------------------------------------------------------------------

constexpr uint32_t CONTROL_STATUS_REFRESH_WAIT_MS = 50;
constexpr uint32_t LOOP_NO_SCALE_DELAY_MS = 5;
// Persist blob is PSRAM BSS, not a 3 KiB stack local. Keep headroom for
// Preferences / TWDT on the flash-writing task (stack stays internal).
constexpr uint32_t SETTINGS_PERSIST_TASK_STACK_SIZE = 4096;
constexpr uint32_t SCALE_STOP_RETRY_INTERVAL_MS = 250;
constexpr uint32_t SCALE_STOP_RETRY_WINDOW_MS = 5000;
constexpr uint8_t SCALE_STOP_MAX_ATTEMPTS = 3;
constexpr uint32_t MAINTENANCE_LEASE_SETTLE_MS = 100;
constexpr uint32_t RUNTIME_PERSIST_RETRY_MS = 500;
constexpr uint32_t SHOT_STORE_PERSIST_RETRY_MS = 500;
constexpr uint32_t PERSIST_IO_RETRY_MAX_MS = 30000;
constexpr uint32_t RUNTIME_PERSIST_DEBOUNCE_MS = 300;
constexpr uint32_t SETTINGS_PERSIST_IDLE_WAIT_MS = 1000;
constexpr UBaseType_t PERSISTENCE_WORK_QUEUE_DEPTH = 4;
// Pin control/BLE/LED work with Arduino loopTask on APP_CPU (core 1).
// network_manager is pinned to PRO_CPU (core 0) in ShotStopperNetwork.cpp.
constexpr BaseType_t CONTROL_TASK_CORE = 1;
constexpr BaseType_t PERSISTENCE_TASK_CORE = 0;
static_assert(SCALE_WORKER_TASK_CORE == CONTROL_TASK_CORE,
              "Scale worker must stay pinned with the control/BLE task");

static_assert(SCALE_WORKER_STALE_MS > ACTIVATOR_DEBOUNCE_MS &&
                  SCALE_WORKER_STALE_MS < HARD_MAX_CIRCUIT_CLOSED_MS,
              "Scale worker stale timeout must be useful and safety-bounded");
static_assert(FLASH_IO_CONTROL_LOCK_TIMEOUT_MS * 20U <
                  TASK_WATCHDOG_TIMEOUT_MS,
              "Control flash lock must stay well under the task watchdog");
static_assert(BLE_CONNECT_TIMEOUT_MS + SCALE_ATT_TIMEOUT_MS <
                  TASK_WATCHDOG_TIMEOUT_MS,
              "GAP connect plus one ATT wait must fit under the task watchdog");
static_assert(BLE_DISCOVER_TIMEOUT_MS < TASK_WATCHDOG_TIMEOUT_MS,
              "GATT discovery must fit under the task watchdog");
static_assert(SCALE_STREAM_GAP_MS < MAX_AUTOMATION_WEIGHT_AGE_MS,
              "Stream gaps must stay below the 1 s STALE window");
static_assert(CONTROL_STATUS_REFRESH_WAIT_MS >= 20U &&
                  CONTROL_STATUS_REFRESH_WAIT_MS <= 50U,
              "GET snapshot wait must cover one control tick without hanging httpd");

// ---------------------------------------------------------------------------
// Persistent storage and scale prediction
// ---------------------------------------------------------------------------

constexpr size_t EEPROM_SIZE = 2;
constexpr size_t WEIGHT_ADDR = 0;
constexpr size_t OFFSET_ADDR = 1;
constexpr size_t TREND_POINT_COUNT = WEIGHT_TREND_POINT_COUNT;
static_assert(MAX_SHOT_DATAPOINTS >= WEIGHT_TREND_POINT_COUNT,
              "Shot trajectory must hold the prediction window");
static_assert(SHOT_CURVE_MAX_POINTS == 121,
              "ControlStatusSnapshot shotCurveWeightCg must match sampler");

bool startExtendedPulseTrain(uint32_t durationMs);
bool startPulseTrain(BuzzerPattern pattern, uint32_t durationMs);
bool emitAlert(AlertEvent event, uint32_t cycleId = 0);
bool commandAlertUsesBuzzer();
void emitImmediateCommandAlertIfBuzzer(AlertEvent event);
bool emitCircuitCycleAlert(AlertEvent event, bool preempt);
void emitCommandAlert(AlertEvent event, bool commandAttempted,
                      bool writeSucceeded, bool commandFeedbackExpected);

enum class TimerStopResult : uint8_t {
  NOT_REQUIRED,
  NOT_ATTEMPTED,
  PENDING,
  WRITE_SUCCEEDED,
  WRITE_FAILED
};

struct ShotTrajectory {
  uint32_t startMs = 0;
  float expectedEndS = DEFAULT_OPERATIONAL_WALL_MS / 1000.0f;
  float weight[MAX_SHOT_DATAPOINTS] = {};
  float timeS[MAX_SHOT_DATAPOINTS] = {};
  size_t datapoints = 0;
  bool automaticBrew = false;
};

struct CycleSession {
  bool active = false;
  bool automaticEnabled = false;
  bool bbwProtectionEnabled = false;
  bool startedWithScale = false;
  bool scaleWasLost = false;
  bool timerStartCommandQueued = false;
  bool remoteTimerMayBeRunning = false;
  bool remoteTimerStarted = false;
  bool remoteTimerStartSettled = false;
  bool stopTimerRequested = false;
  bool stopTimerCommandQueued = false;
  bool receivedFreshWeightInCycle = false;
  bool calibrationEligible = false;
  bool hasWeightAnchor = false;
  bool directStopPending = false;
  bool awaitingPostTareBaseline = false;
  TimerStopResult timerStopResult = TimerStopResult::NOT_REQUIRED;
  WeightControlState weightControlState = WeightControlState::INACTIVE;
  EndReason directStopReason = EndReason::NONE;
  uint8_t stopTimerAttempts = 0;
  uint8_t thresholdConfirmations = 0;
  uint8_t recoveryConfirmations = 0;
  uint32_t id = 0;
  uint32_t scaleDisconnectSequenceAtStart = 0;
  uint32_t weightSequenceAtStart = 0;
  uint32_t connectionGenerationAtStart = 0;
  uint32_t ownedConnectionGeneration = 0;
  uint32_t startedAtMs = 0;
  uint32_t circuitClosedAtMs = 0;
  // Mirror of rinseClockStartedAtMs after the stopper accepts. Tests only.
  uint32_t rinseStartedAtMs = 0;
  uint32_t postTareBaselineDeadlineMs = 0;
  uint32_t stopTimerRetryDeadlineMs = 0;
  uint32_t stopTimerLastAttemptMs = 0;
  uint32_t lastAcceptedWeightAtMs = 0;
  uint32_t lastAcceptedPacketSequence = 0;
  uint32_t lastThresholdAtMs = 0;
  uint32_t lastThresholdPacketSequence = 0;
  uint32_t lastThresholdConnectionGeneration = 0;
  uint32_t recoveryLastAtMs = 0;
  uint32_t recoveryLastPacketSequence = 0;
  uint32_t firstDropMs = 0;
  float scaleBaselineG = 0.0f;
  bool scaleBaselineReady = false;
  float lastAcceptedWeightG = 0.0f;
  float recoveryLastWeightG = 0.0f;
  bool retareEnded = false;
  bool bbwProtectionEnded = false;
  bool flowDuringRetare = false;
  uint32_t retareFlowFirstDetectedAtMs = 0;
  bool retarePerformed = false;
  bool retareDisabled = false;
  bool firstDropsBeepSent = false;
  FirstFlowState firstFlow = {};
  ControlSource source = ControlSource::NONE;
  CycleConfigSnapshot config = {};
  EndReason endReason = EndReason::NONE;
  bool extractionExtended = false;
  bool slowExtractionExtended = false;
  bool targetReachedEarly = false;
  uint32_t targetReachedAtMs = 0;
  bool autoToManualGuardArmed = false;
  bool autoToManualGuardEnforced = false;
  uint32_t autoToManualGuardDeadlineAtMs = 0;
  uint8_t activePresetId = 0;
  char activePresetName[SHOT_PRESET_NAME_CAPACITY] = {};
  bool cupRemovedPending = false;
  AccidentalTouchPhase accidentalTouchPhase = AccidentalTouchPhase::STARTUP;
  AccidentalTouchClass accidentalTouchClass = AccidentalTouchClass::OK;
  bool accidentalTouchHolding = false;
  uint8_t accidentalTouchPendingCount = 0;
  float accidentalTouchPendingG[ACCIDENTAL_TOUCH_SUSTAINED_SAMPLES] = {};
};

struct PendingShotFinalize {
  uint8_t bbwAlgorithm = 0;
  uint8_t bbwAlpha = 100;
  uint8_t bbwProfileVersion = 1;
  uint32_t bbwLearningGeneration = 0;
  uint32_t scaleConnectionGeneration = 0;
  bool scaleBaselineReady = false;
  bool bbwLearningApplied = false;
  bool pending = false;
  bool offsetAnalysis = false;
  bool logEligible = false;
  uint32_t endedAtMs = 0;
  uint32_t dripDelayMs = DEFAULT_DRIP_DELAY_MS;
  uint32_t endedWeightSequence = 0;
  uint32_t cycleStartedAtMs = 0;
  uint32_t bootId = 0;
  uint16_t durationDs = 0;
  uint16_t firstDropDs = SHOT_LOG_METRIC_MISSING;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  float weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  float scaleBaselineG = 0.0f;
  bool startedWithScale = false;
  bool timerOnly = false;
  bool automaticBrew = false;
  StopperState finalState = StopperState::READY;
  EndReason endReason = EndReason::NONE;
  bool extractionGuardEnabled = false;
  bool extractionExtended = false;
  bool slowExtractionGuardEnabled = false;
  bool slowExtractionExtended = false;
  bool targetReachedEarly = false;
  uint16_t targetReachedEarlyDs = SHOT_LOG_METRIC_MISSING;
  float maxRecoveryWeightG = DEFAULT_MAX_RECOVERY_WEIGHT_G;
  uint32_t minBbwBrewTimeMs = DEFAULT_MIN_BBW_BREW_TIME_MS;
  uint32_t protectionMs = DEFAULT_BBW_PROTECTION_MS;
  bool lastKnownWeightValid = false;
  float lastKnownWeightG = 0.0f;
  uint8_t activePresetId = 0;
  char activePresetName[SHOT_PRESET_NAME_CAPACITY] = {};
  uint32_t cycleId = 0;
  bool retarePerformed = false;
  float minRecoveryWeightG = DEFAULT_MIN_RECOVERY_WEIGHT_G;
  bool autoToManualGuardEnabled = false;
  bool autoToManualGuardEnforced = false;
  bool autoToManualGuardArmed = false;
  uint32_t autoToManualGuardRemainingMs = 0;
  bool noScaleShotGuardEnabled = false;
  bool noScaleShotGuardArmed = false;
  uint8_t noScaleBbwMode = static_cast<uint8_t>(NoScaleBbwMode::OFF);
  char scaleProtocol[20] = "none";
  ShotCurveRecord curve = {};
};

struct MaintenanceLease {
  bool active = false;
  bool forwarded = false;
  bool applyRuntimeOnSuccess = false;
  uint32_t id = 0;
  uint32_t startedAtMs = 0;
  WebCommand command = {};
};

// ---------------------------------------------------------------------------
// Application state and input state
// ---------------------------------------------------------------------------

#if !defined(SHOT_STOPPER_HOST_TEST)
BleScanPersistedSettings bleScanPersistedSettings;
#endif

StopperState stopperState = StopperState::REQUIRES_OFF;
ShotTrajectory shot;
CycleSession session;
PendingShotFinalize pendingFinalize;
RuntimeConfig runtimeConfig;
PowerPolicy powerPolicy;
SHOT_STOPPER_PSRAM_BSS BullseyeMelodyConfig bullseyeMelodyConfig;
SHOT_STOPPER_PSRAM_BSS BullseyeMelodyConfig stagedBullseyeMelodyConfig;
uint32_t stagedBullseyeRequestId = 0;
TaskMutex bullseyeConfigMux;
BullseyeTracker bullseyeTracker;
LocalBuzzer localBuzzer;
ShotPresetBank presetBank;
BbwLearningBank bbwLearningBank;
LastCycleSummary lastCycle;
// Debug events are not a flash DMA source and are not added from an ISR.
SHOT_STOPPER_PSRAM_BSS DebugRingBuffer debugLog;
// The retained ring is task-only. A priority-inheriting mutex avoids disabling
// interrupts while HTTP/CLI readers copy records from PSRAM.
TaskMutex debugLogMutex;
LogLevel serialLogLevel = LogLevel::NONE;
LogLevel ringRetainLogLevel = LogLevel::NONE;
// Working copies: NVS/partition I/O copies through internal flash scratch
// first. Safe in PSRAM BSS because putBytes/erase/write never DMA these.
// ActivationStores is the single owner of the three activation ring stores;
// the aliases below keep every existing call site and test working against
// that owner. Static-init order is safe: PSRAM BSS plus references bound to
// constant addresses.
SHOT_STOPPER_PSRAM_BSS ActivationStores activationStores;
ShotLog &shotLog = activationStores.shotLog;
ShotCurveLog &shotCurves = activationStores.shotCurves;
HistoryLog &historyLog = activationStores.historyLog;
// Serializes complete RAM-store operations across control and NetworkService;
// every durable write remains owned by the existing store/flash path.
TaskMutex shotStoreMutex;
ShotCurveSampler shotCurveSampler;
// Same PSRAM-safe working-copy contract as ActivationStores above: NVS I/O
// goes through the internal flash scratch and mutations hold shotStoreMutex.
SHOT_STOPPER_PSRAM_BSS LastShotStore lastShotStore;
const PersistedLastShot &persistedLastShot = lastShotStore.get();
const PersistedLastShot &persistedLastGoodShot = lastShotStore.getGood();
bool lastShotNvsDirty = false;
bool lastShotPersistFailLatched = false;
bool controllerStartedPending = false;
uint32_t shotStorePersistRetryAtMs = 0;
uint32_t shotStorePersistIoRetryMs = 0;
std::atomic<uint32_t> shotStoreDirtyGeneration{0};
uint32_t shotStoreObservedDirtyGeneration = 0;

bool noScaleShotGuardArmed = true;
uint32_t noScaleShotGuardActivityAtMs = 0;
bool noScaleShotGuardScaleWasAvailable = false;
bool noScaleShotGuardHold = false;
uint32_t noScaleShotGuardHoldAtMs = 0;
bool noScaleShotGuardNeedsFreshActivator = false;
bool noScaleRequireBypassReady = false;
bool noScaleRequireBypassHoldSeen = false;
uint8_t noScaleRequireBypassCycles = 0;
uint32_t noScaleRequireBypassStartedAtMs = 0;
bool noScaleRequireBypassCompletedThisLoop = false;
bool cupStartGuardHold = false;
uint32_t cupStartGuardHoldAtMs = 0;
bool machineWakePassthroughActive = false;
uint32_t machineWakeStartedAtMs = 0;
bool machineWakeGestureConsumedThisLoop = false;

float currentWeight = 0.0f;
uint32_t currentWeightReceivedAtMs = 0;
uint32_t currentWeightSequence = 0;
uint32_t currentWeightConnectionGeneration = 0;
float observedWeight = 0.0f;
uint32_t observedWeightReceivedAtMs = 0;
uint32_t observedWeightSequence = 0;
uint32_t observedWeightConnectionGeneration = 0;
WeightStreamState weightStreamState = WeightStreamState::NO_SAMPLE;
uint32_t nextCycleId = 1;
QueueHandle_t webCommandQueue = nullptr;
uint32_t debugLogContentionDropped = 0;
// Snapshot of ring overwrites only. Contention drops are a separate monotonic
// atomic counter so a producer cannot overwrite another producer's increment.
uint32_t debugLogDroppedSnapshot = 0;
bool pendingCupRemovedSettle = false;
uint32_t scaleRecoveredStaleCount = 0;
uint32_t scaleRecoveredStaleMs = 0;
bool recoverableStaleOpen = false;
uint32_t recoverableStaleStartedAtMs = 0;
uint32_t recoverableStaleDisconnectSequence = 0;
uint32_t recoverableStaleConnectionGeneration = 0;
WeightStreamState telemetryWeightStreamState = WeightStreamState::NO_SAMPLE;
bool scaleCompletionBeepScheduled = false;
// Control-owned cursors over the worker-published monotonic link counters.
// They let control observe every kind of edge without touching worker state.
uint32_t handledScaleConnectionGeneration = 0;
uint32_t handledScaleDisconnectSequence = 0;
#ifndef SHOT_STOPPER_HOST_TEST
bool webhookBrewStartPending = false;
#endif

bool virtualHoldOn = false;
// Full status snapshots stay in internal DRAM (check_web_assets forbids
// SHOT_STOPPER_PSRAM_BSS on this type). Task mutexes make the ordinary C++
// payload copies race-free; readers never accept an in-progress publication.
ControlStatusSnapshot publishedControlStatus;
TaskMutex controlStatusMutex;
uint32_t controlStatusVersion = 0;
ControlGateSnapshot publishedControlGate;
TaskMutex controlGateMutex;
bool controlStatusPublishRequested = false;
SHOT_STOPPER_PSRAM_BSS RuntimeConfig publishedRuntimeConfig;
SHOT_STOPPER_PSRAM_BSS ShotPresetBank publishedPresetBank;
TaskMutex recipeMutex;
BootCapabilities bootCapabilities;
BootState bootState = BootState::BOOTING;
bool bootDegraded = false;
MaintenanceLease maintenanceLease;
WebCommand maintenanceCancellationCommand;
bool maintenanceCancellationPending = false;
// Planned ESP.restart (OTA flash, Admin, CLI) waits here while a shot is
// running. The paddle is never blocked; the restart proceeds once idle.
bool plannedRestartHeld = false;
WebCommand pendingPlannedRestart = {};
bool resetHistoryClearHeld = false;
WebCommand pendingResetHistoryClear = {};
WebCommand controlResultCommand;
bool controlResultPending = false;
#ifdef SHOT_STOPPER_HOST_TEST
bool hostForwardAcceptedNetworkCommandSucceeds = true;
uint32_t hostForwardAcceptedNetworkCommandCalls = 0;
WebCommand hostLastForwardedNetworkCommand;
uint32_t hostNtpSyncRequestCount = 0;
bool hostRuntimePersistSucceeds = true;
uint32_t hostRuntimePersistAttempts = 0;
RuntimeConfig hostLastFlushedRuntime;
ShotPresetBank hostLastFlushedPresets;
bool hostLastFlushIncludedLive = false;
bool hostLastMaintenanceSucceeded = false;
bool hostSettingsPersistQueueCreateSucceeds = true;
bool hostSettingsPersistTaskCreateSucceeds = true;
uint32_t hostSettingsPersistRollbackDeletes = 0;
uint32_t hostPresetWebhookCount = 0;
uint32_t hostQuickSettingsWebhookCount = 0;
uint32_t hostControllerStartedWebhookCount = 0;
bool hostControllerStartedWebhookSucceeds = true;
bool (*hostControllerStartedWebhookBuildGuard)() = nullptr;
WebhookEvent hostControllerStartedWebhookEvent;
#endif
bool runtimePersistPending = false;
bool runtimePersistFailed = false;
struct PendingPresetPersistence {
  uint32_t requestId = 0;
  uint32_t revision = 0;
  bool notifyPresets = false;
  bool applyMachineTemperature = false;
};
PendingPresetPersistence pendingPresetPersistence;
bool quickSettingsPersistPending = false;
RuntimeConfig runtimePersistCandidate;
uint32_t runtimePersistRetryAtMs = 0;
uint32_t runtimePersistIoRetryMs = 0;
int32_t runtimePersistReasonBits = 0;
uint32_t nextInternalRequestId = 0x80000000UL;
enum class PersistenceWork : uint8_t { SETTINGS = 1, SHOT_STORES = 2 };
#ifndef SHOT_STOPPER_HOST_TEST
SHOT_STOPPER_PSRAM_BSS SettingsPersistRequest settingsPersistRequest;
TaskMutex settingsPersistMux;
bool settingsPersistInFlight = false;
bool settingsPersistResultReady = false;
bool settingsPersistResultOk = false;
bool settingsPersistResultLockContended = false;
uint32_t settingsPersistResultRuntimeRevision = 0;
uint32_t settingsPersistResultStorageRevision = 0;
#endif
QueueHandle_t settingsPersistQueue = nullptr;
TaskHandle_t settingsPersistTaskHandle = nullptr;
bool settingsPersistenceReady = false;
ActivationStores *shotStorePersistImage = nullptr;
LastShotStore *lastShotPersistImage = nullptr;
TaskMutex shotStorePersistMux;
bool shotStorePersistInFlight = false;
bool shotStorePersistResultReady = false;
bool shotStorePersistResultOk = false;
bool shotStorePersistResultIoFail = false;
bool shotStorePersistImageLastShotDirty = false;
uint32_t shotStorePersistImageGeneration = 0;
uint32_t shotStorePersistResultGeneration = 0;
// Staged BLE scan settings: the control loop publishes, the settings_persist
// worker owns the durable NVS write. pending/intensity/result flags are
// guarded by bleScanPersistMux. Every request id accepted since the last
// flush is covered by that combined save and reports PERSISTED in FIFO order;
// a full ring drops the oldest id. Factory reset clears all staged state.
constexpr uint8_t PENDING_BLE_SCAN_REQUEST_CAPACITY = 4;
TaskMutex bleScanPersistMux;
bool bleScanPersistPending = false;
uint8_t bleScanPersistIntensity = 0;
bool bleScanBackoffPersistPending = false;
uint8_t bleScanPersistBackoffMin = SCALE_SCAN_QUIET_BACKOFF_DEFAULT_MIN;
bool bleScanBoostPersistPending = false;
uint8_t bleScanPersistBoostMin = SCALE_SCAN_BOOST_DEFAULT_MIN;
bool bleScanPersistResultReady = false;
bool bleScanPersistResultOk = false;
uint32_t pendingBleScanRequestIds[PENDING_BLE_SCAN_REQUEST_CAPACITY] = {};
uint8_t pendingBleScanRequestIdCount = 0;
bool bleScanPersistFailLatched = false;
uint32_t lastLoopAtMs = 0;
uint32_t loopMaxGapMs = 0;
uint32_t loopDeadlineMisses = 0;
uint32_t loopMaxExecutionUs = 0;
uint32_t loopIntervalGapMs = 0;
uint32_t healthIntervalMaxGapMs = 0;
uint32_t loopStackMinBytes = UINT32_MAX;
constexpr uint32_t HEALTH_SAMPLE_STALE_MS = 3U * HEALTH_TELEMETRY_INTERVAL_MS;
enum class HealthProfilerRequest : uint8_t { NONE, START, STOP };
struct HealthWorkerSample {
  uint32_t version = 0;
  uint32_t sampledAtMs = 0;
  bool heapValid = false;
  bool restartRequested = false;
  HeapCapSnapshot heap = {};
  uint8_t bleRuntimeState = 0;
  int32_t bleRuntimeLastError = 0;
  int32_t bleRuntimeLastResetReason = 0;
  uint32_t bleRuntimeSyncGeneration = 0;
  uint32_t bleRuntimeResetCount = 0;
  uint32_t bleRuntimeHostStackMinBytes = UINT32_MAX;
  HwmonSnapshot hwmon = {};
};
QueueHandle_t healthSampleQueue = nullptr;
TaskHandle_t healthTaskHandle = nullptr;
std::atomic<HealthProfilerRequest> healthProfilerRequest{
    HealthProfilerRequest::NONE};
uint32_t healthSnapshotVersion = 0;
uint32_t healthSnapshotAtMs = 0;
bool healthSnapshotValid = false;
uint32_t freeHeapBytes = 0;
uint32_t minimumFreeHeapBytes = 0;
uint32_t largestFreeHeapBlockBytes = 0;
uint32_t internalHeapAllocatedBlocks = 0;
uint32_t internalHeapFreeBlocks = 0;
uint16_t internalHeapFragmentationPermille = 0;
uint32_t psramSizeBytes = 0;
uint32_t psramFreeBytes = 0;
uint32_t psramMinimumFreeBytes = 0;
uint32_t psramLargestFreeBlockBytes = 0;
uint32_t bleHostAllocPsramCount = 0;
uint32_t bleHostAllocFallbackCount = 0;
uint32_t bleHostHciRxDropped = 0;
uint32_t bleHostHciTxDropped = 0;
uint8_t bleRuntimeState = 0;
int32_t bleRuntimeLastError = 0;
int32_t bleRuntimeLastResetReason = 0;
uint32_t bleRuntimeSyncGeneration = 0;
uint32_t bleRuntimeResetCount = 0;
uint32_t bleRuntimeHostStackMinBytes = UINT32_MAX;
bool healthHeapAlertLatched = false;
bool healthHeapRestartLatched = false;
bool healthStackAlertLatched = false;
bool healthLoopGapAlertLatched = false;
Hwmon hwmon;
HwmonSnapshot hwmonSnapshot = {};
TaskProfiler taskProfiler;
LoopPhaseProfiler loopPhaseProfiler;
bool platformClockReady = false;
bool persistenceReady = false;
bool firmwareInitializationComplete = false;
bool serialLogSinkInstalled = false;
bool serialLogSinkEnabled = false;
std::atomic<bool> controlCriticalForLogging{false};

#include "diagnostics/ShotStopperSerialOutput.inc"

SHOT_STOPPER_PSRAM_BSS DebugEvent serialLogDumpSnapshot[DEBUG_EVENT_CAPACITY];
size_t serialLogDumpCount = 0;
size_t serialLogDumpIndex = 0;

LogLevel currentSerialLogLevel() {
  LogLevel level;
  __atomic_load(&serialLogLevel, &level, __ATOMIC_ACQUIRE);
  return level;
}

LogLevel currentRingRetainLogLevel() {
  LogLevel level;
  __atomic_load(&ringRetainLogLevel, &level, __ATOMIC_ACQUIRE);
  return level;
}

void publishLogLevels(LogLevel serialLevel, LogLevel ringLevel) {
  __atomic_store(&ringRetainLogLevel, &ringLevel, __ATOMIC_RELEASE);
  __atomic_store(&serialLogLevel, &serialLevel, __ATOMIC_RELEASE);
}

bool beginMaintenanceLease(const WebCommand &networkCommand,
                           bool applyRuntimeOnSuccess);
void completeMaintenanceLease(const WebCommand &result);
void rejectWebCommand(const WebCommand &command);
void holdOrBeginPlannedRestart(const WebCommand &command);
void servicePendingPlannedRestart();
void holdOrBeginResetHistoryClear(const WebCommand &command);
void servicePendingResetHistoryClear();
void queueRuntimePersist(int32_t reasonBits);
void commitLiveRuntimeConfig(const RuntimeConfig &composed, int32_t reasonBits);
bool settingsPersistenceAvailable();
void serviceHealthThresholdAlerts(uint32_t intervalMaxGapMs);

WebhookEvent baseWebhookEvent(WebhookEventType type, uint32_t cycleId,
                              uint32_t occurredAtMs);
#ifndef SHOT_STOPPER_HOST_TEST
SHOT_STOPPER_PSRAM_BSS PersistedSettings persistedSettings;
ShotStopperNetwork networkManager;

bool enqueueControllerStartedWebhook() {
  if (!controllerStartedPending) return true;
  WebhookEvent started =
      baseWebhookEvent(WebhookEventType::CONTROLLER_STARTED, 0, millis());
  started.presetRevision = runtimeConfig.revision;
  if (!networkManager.enqueueWebhook(started)) return false;
  controllerStartedPending = false;
  return true;
}

void syncScaleWorkerNetworkRf(bool scaleLinkOrConnecting,
                              bool scaleConnectingNow,
                              bool huntWindowActive) {
  networkManager.syncScaleLinkRf(scaleLinkOrConnecting);
  networkManager.syncScaleConnectingRf(scaleConnectingNow);
  networkManager.syncScaleHuntRf(huntWindowActive);
}
#endif

#ifdef SHOT_STOPPER_HOST_TEST
bool enqueueControllerStartedWebhook() {
  if (!controllerStartedPending) return true;
  if (!hostControllerStartedWebhookSucceeds) return false;
  if (hostControllerStartedWebhookBuildGuard != nullptr &&
      !hostControllerStartedWebhookBuildGuard()) {
    return false;
  }
  hostControllerStartedWebhookEvent =
      baseWebhookEvent(WebhookEventType::CONTROLLER_STARTED, 0, millis());
  ++hostControllerStartedWebhookCount;
  controllerStartedPending = false;
  return true;
}
#endif

// The esp_timer callback independently opens the machine circuit at the hard limit even if the
// normal control loop is delayed or unavailable.
esp_timer_handle_t relaySafetyTimer = nullptr;
esp_timer_handle_t operationalLimitTimer = nullptr;
#ifndef SHOT_STOPPER_HOST_TEST
DRAM_ATTR IndependentSafetyTimer independentSafetyTimer;
#else
IndependentSafetyTimer independentSafetyTimer;
#endif
portMUX_TYPE relayMux = portMUX_INITIALIZER_UNLOCKED;
bool circuitClosed = false;
bool relaySafetyTripped = false;
bool operationalLimitTripped = false;
uint32_t circuitClosedAtMs = 0;
struct PendingScaleTimerStop {
  bool pending = false;
  uint32_t targetMs = 0;
  uint32_t extraDelayMs = 0;
  uint32_t catchupDeadlineAtMs = 0;
  uint32_t extraDueAtMs = 0;
};
PendingScaleTimerStop pendingScaleTimerStop;
bool pendingBrewRfRestore = false;
uint32_t operationalLimitAtArmMs = HARD_MAX_CIRCUIT_CLOSED_MS;
RelaySafetyState relaySafetyState = RelaySafetyState::BOOT_SAFE;
RelaySafetyFault relaySafetyFault = RelaySafetyFault::NONE;
uint32_t relaySafetyGeneration = 0;
bool relaySafetyTimersReady = false;
bool taskWatchdogReady = false;
volatile bool feedbackTransitionPending = false;
volatile bool feedbackExpectedClosed = false;
volatile uint32_t feedbackTransitionStartedAtMs = 0;
volatile bool feedbackTransitionStampPending = false;
bool safetyHeartbeatLevel = false;
uint32_t safetyHeartbeatToggledAtMs = 0;
uint32_t bootStartedAtMs = 0;
SafetyResetSnapshot safetyResetStatus;
UsbSerialEnableSource usbSerialEnableSource = UsbSerialEnableSource::OFF;

bool usbConsoleJumperPresent();
RelaySafetySnapshot getRelaySafetySnapshot();

bool scaleConnectedLedInitialized = false;
bool lastScaleConnectedLedOn = false;
enum class ScaleConnectedLedPattern : uint8_t {
  OFF,
  SOLID,
  FAST_BLINK,
  SLOW_BLINK
};
ScaleConnectedLedPattern lastScaleConnectedLedPattern =
    ScaleConnectedLedPattern::OFF;
uint32_t lastScaleConnectedLedToggleAtMs = 0;

// ---------------------------------------------------------------------------
// Utility helpers
// ---------------------------------------------------------------------------

uint32_t elapsedMs(uint32_t sinceMs) {
  return static_cast<uint32_t>(millis() - sinceMs);
}

const char *espLogTag(DebugCategory category) {
  switch (category) {
    case DebugCategory::ACTIVATOR: return "ss.activator";
    case DebugCategory::RELAY: return "ss.relay";
    case DebugCategory::STATE: return "ss.state";
    case DebugCategory::SCALE: return "ss.scale";
    case DebugCategory::CONFIG: return "ss.config";
    case DebugCategory::NETWORK: return "ss.network";
    case DebugCategory::SECURITY: return "ss.security";
    case DebugCategory::WEB: return "ss.web";
    case DebugCategory::BOOT: return "ss.boot";
    case DebugCategory::SYSTEM: return "ss.system";
  }
  return "ss.unknown";
}

#if !defined(SHOT_STOPPER_HOST_TEST)
esp_log_level_t espLogLevel(LogLevel level) {
  switch (level) {
    // ESP-IDF has no critical/fatal level. Keep that distinction in the
    // application record and mark it in the emitted message.
    case LogLevel::CRITICAL:
    case LogLevel::ERROR: return ESP_LOG_ERROR;
    case LogLevel::WARNING: return ESP_LOG_WARN;
    case LogLevel::INFO: return ESP_LOG_INFO;
    case LogLevel::DEBUG: return ESP_LOG_DEBUG;
    case LogLevel::NONE: return ESP_LOG_NONE;
  }
  return ESP_LOG_NONE;
}

int shotStopperEspLogVprintf(const char *format, va_list args) {
  if (!__atomic_load_n(&serialLogSinkEnabled, __ATOMIC_ACQUIRE) ||
      format == nullptr) {
    return 0;
  }
  // esp_log may invoke this callback concurrently. Never perform USB I/O in
  // the caller: CDC can block indefinitely when the host stops draining.
  SerialLogLine line;
  va_list copy;
  va_copy(copy, args);
  const int formatted = vsnprintf(line.text, sizeof(line.text), format, copy);
  va_end(copy);
  if (formatted <= 0) {
    return formatted;
  }
  size_t length = static_cast<size_t>(formatted);
  if (length >= sizeof(line.text)) {
    length = sizeof(line.text) - 1;
    (void)__atomic_add_fetch(&serialLogQueueTruncated, 1U, __ATOMIC_RELAXED);
  }
  line.length = static_cast<uint16_t>(length);
  if (serialLogQueue == nullptr ||
      xQueueSend(serialLogQueue, &line, 0) != pdTRUE) {
    (void)__atomic_add_fetch(&serialLogQueueDropped, 1U, __ATOMIC_RELAXED);
    return 0;
  }
  return static_cast<int>(length);
}

void serialLogTask(void *) {
  SerialLogLine line;
  for (;;) {
    if (drainSerialCliOutput()) continue;
    if (xQueueReceive(serialLogQueue, &line, pdMS_TO_TICKS(10)) == pdTRUE &&
        line.length > 0) {
      (void)Serial.write(reinterpret_cast<const uint8_t *>(line.text),
                         line.length);
    }
  }
}

bool initializeSerialLogSink() {
  if (serialLogQueue != nullptr && serialLogTaskHandle != nullptr) {
    return true;
  }
  constexpr size_t queueBytes = SERIAL_LOG_QUEUE_DEPTH * sizeof(SerialLogLine);
  serialLogQueueBytes = static_cast<uint8_t *>(
      allocInternal(queueBytes, AllocationOwner::SERIAL_LOG));
  if (serialLogQueueBytes == nullptr || !esp_ptr_internal(serialLogQueueBytes)) {
    heapCapsFree(serialLogQueueBytes);
    serialLogQueueBytes = nullptr;
    return false;
  }
  serialLogQueue = xQueueCreateStatic(
      SERIAL_LOG_QUEUE_DEPTH, sizeof(SerialLogLine), serialLogQueueBytes,
      &serialLogQueueStorage);
  if (serialLogQueue == nullptr) {
    releaseSerialLogStorage();
    return false;
  }
  serialCliOutputBytes = static_cast<uint8_t *>(
      allocExternal(SERIAL_CLI_OUTPUT_CAPACITY, AllocationOwner::SERIAL_LOG));
  if (serialCliOutputBytes == nullptr) {
    releaseSerialLogStorage();
    return false;
  }
  if (xTaskCreatePinnedToCore(serialLogTask, "serial_log", 3072, nullptr,
                             tskIDLE_PRIORITY, &serialLogTaskHandle, 0) !=
      pdPASS) {
    releaseSerialLogStorage();
    serialLogTaskHandle = nullptr;
    return false;
  }
  return true;
}

void installEspLogSink() {
  if (!serialLogSinkInstalled && initializeSerialLogSink()) {
    (void)esp_log_set_vprintf(shotStopperEspLogVprintf);
    serialLogSinkInstalled = true;
  }
}

void configureEspLogRuntime() {
  const LogLevel applicationLevel = currentSerialLogLevel();
  const bool enabled = applicationLevel != LogLevel::NONE;
  // Close the sink first when disabling so another task cannot emit under the
  // old tag level while this update is in progress.
  if (!enabled) {
    __atomic_store_n(&serialLogSinkEnabled, false, __ATOMIC_RELEASE);
  }
  const esp_log_level_t level = espLogLevel(applicationLevel);
  static const char *const tags[] = {
      "ss.activator", "ss.relay", "ss.state", "ss.scale", "ss.config",
      "ss.network", "ss.security", "ss.web", "ss.boot", "ss.system"};
  for (const char *tag : tags) {
    esp_log_level_set(tag, level);
  }
  if (enabled) {
    __atomic_store_n(&serialLogSinkEnabled, true, __ATOMIC_RELEASE);
  }
}
#else
void installEspLogSink() {}
void configureEspLogRuntime() {}
#endif

void latchControlCriticalLogging() {
  controlCriticalForLogging.store(true, std::memory_order_release);
}

void publishControlCriticalLoggingState() {
  const bool critical = session.active || getRelaySafetySnapshot().closed;
  controlCriticalForLogging.store(critical, std::memory_order_release);
}

bool serialApplicationLogAllowed() {
#if defined(SHOT_STOPPER_HOST_TEST)
  // Portable tests directly manipulate the control-owned fixtures.
  return !session.active && !circuitClosed;
#else
  return !controlCriticalForLogging.load(std::memory_order_acquire);
#endif
}

void emitEspLog(LogLevel level, DebugCategory category, const char *message) {
  if (message == nullptr || level == LogLevel::NONE) {
    return;
  }
#if !defined(SHOT_STOPPER_HOST_TEST)
  const char *tag = espLogTag(category);
  const char *criticalPrefix =
      level == LogLevel::CRITICAL ? "[CRITICAL] " : "";
  // ESP_LOG_LEVEL accepts a runtime level without the ESP_LOGx local-level
  // compile-out and adds the standard severity, timestamp, tag and newline.
  // cppcheck-suppress syntaxError ; older Cppcheck cannot parse this ESP-IDF macro.
  ESP_LOG_LEVEL(espLogLevel(level), tag, "%s%s", criticalPrefix, message);
#else
  (void)category;
  Serial.println(message);
#endif
}

void formatDebugEventMessage(const DebugEvent &event, char *message,
                             size_t capacity) {
  if (message == nullptr || capacity == 0) {
    return;
  }
  if (event.code == DebugCode::LOG_TEXT) {
    copyCString(message, capacity, event.text);
    return;
  }
  if (event.code == DebugCode::BOOT_BANNER) {
    snprintf(message, capacity, "Advanced Shot Stopper %s (bootId=%ld)",
             FW_VERSION, static_cast<long>(event.argument1));
    return;
  }
  if (event.code == DebugCode::STATE_TRANSITION &&
      event.argument1 >= static_cast<int32_t>(StopperState::REQUIRES_OFF) &&
      event.argument1 <= static_cast<int32_t>(StopperState::MANUAL_NO_SCALE) &&
      event.argument2 >= static_cast<int32_t>(StopperState::REQUIRES_OFF) &&
      event.argument2 <= static_cast<int32_t>(StopperState::MANUAL_NO_SCALE)) {
    snprintf(message, capacity, "%s -> %s",
             stopperStateName(static_cast<StopperState>(event.argument1)),
             stopperStateName(static_cast<StopperState>(event.argument2)));
    return;
  }
  if (formatScaleSampleDebugMessage(event, message, capacity)) {
    return;
  }
  if (formatPersistDebugMessage(event, message, capacity)) {
    return;
  }
  if (formatLifecycleDebugMessage(event, message, capacity)) {
    return;
  }
  if ((event.code == DebugCode::WEB_COMMAND_ACCEPTED ||
       event.code == DebugCode::WEB_COMMAND_REJECTED) &&
      event.argument1 >= static_cast<int32_t>(WebCommandType::REMOTE_ON) &&
      event.argument1 <=
          static_cast<int32_t>(WebCommandType::MAINTENANCE_COMPLETE)) {
    snprintf(message, capacity, "%s: %s", debugCodeName(event.code),
             webCommandTypeName(
                 static_cast<WebCommandType>(event.argument1)));
    return;
  }
  copyCString(message, capacity, debugCodeName(event.code));
}

void writeSerialLogLine(const DebugEvent &event) {
  char message[128] = {};
  formatDebugEventMessage(event, message, sizeof(message));
  emitEspLog(event.level, event.category, message);
}

void logText(LogLevel level, DebugCategory category, const char *message) {
  if (message == nullptr || level == LogLevel::NONE) {
    return;
  }
  const LogLevel serialThreshold = currentSerialLogLevel();
  const LogLevel ringThreshold = currentRingRetainLogLevel();
  const bool toSerial = logLevelAtMost(level, serialThreshold);
  const bool toRing = logLevelAtMost(level, ringThreshold);
  if (!toSerial && !toRing) {
    return;
  }
  const uint32_t atMs = millis();
  const uint32_t wallSec = g_wallClock.nowUtcSec(atMs);
  if (toRing) {
    {
      TaskLockGuard lock(debugLogMutex);
      debugLog.add(atMs, wallSec, level, category, DebugCode::LOG_TEXT, 0, 0,
                   message);
      __atomic_store_n(&debugLogDroppedSnapshot, debugLog.overwritten(),
                       __ATOMIC_RELAXED);
    }
  }
  if (toSerial && serialApplicationLogAllowed()) {
    emitEspLog(level, category, message);
  }
}

// Ad-hoc diagnostics are application logs too: retain them in the WebUI ring
// and forward them to esp_log. Keep this compatibility wrapper for existing
// call sites while they are gradually made category-specific.
void serialTrace(LogLevel level, const char *message) {
  logText(level, DebugCategory::SYSTEM, message);
}

void serialTraceCategory(LogLevel level, DebugCategory category,
                         const char *message) {
  logText(level, category, message);
}

extern "C" void shotStopperScaleLog(uint8_t severity, const char *message) {
  LogLevel level = LogLevel::DEBUG;
  if (severity == 1) {
    level = LogLevel::WARNING;
  } else if (severity >= 2) {
    level = LogLevel::ERROR;
  }
  serialTraceCategory(level, DebugCategory::SCALE, message);
}

void serialTracef(LogLevel level, const char *fmt, ...) {
  if (fmt == nullptr) {
    return;
  }
  char line[DEBUG_EVENT_TEXT_CAPACITY] = {};
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  logText(level, DebugCategory::SYSTEM, line);
}

void serialTraceCategoryf(LogLevel level, DebugCategory category,
                          const char *fmt, ...) {
  if (fmt == nullptr) {
    return;
  }
  char line[DEBUG_EVENT_TEXT_CAPACITY] = {};
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  logText(level, category, line);
}

void logEmit(LogLevel level, DebugCategory category, DebugCode code,
             int32_t argument1 = 0, int32_t argument2 = 0) {
  if (level == LogLevel::NONE) {
    return;
  }
  const uint32_t atMs = millis();
  const uint32_t wallSec = g_wallClock.nowUtcSec(atMs);
  DebugEvent event;
  event.atMs = atMs;
  event.wallSec = wallSec;
  event.level = level;
  event.category = category;
  event.code = code;
  event.argument1 = argument1;
  event.argument2 = argument2;

  const bool toSerial =
      logLevelAtMost(level, currentSerialLogLevel());
  const bool toRing =
      logLevelAtMost(level, currentRingRetainLogLevel());
  if (!toSerial && !toRing) {
    return;
  }

  {
    TaskLockGuard lock(debugLogMutex);
    if (toRing) {
      debugLog.add(atMs, wallSec, level, category, code, argument1, argument2);
      __atomic_store_n(&debugLogDroppedSnapshot, debugLog.overwritten(),
                       __ATOMIC_RELAXED);
    }
  }

  // Target builds enqueue in the bounded serial sink; host builds emit
  // synchronously into the test stub.
  if (toSerial && serialApplicationLogAllowed()) {
    writeSerialLogLine(event);
  }
}

void addDebugEvent(DebugCategory category, DebugCode code,
                   int32_t argument1 = 0, int32_t argument2 = 0) {
  logEmit(debugCodeDefaultLevel(code), category, code, argument1, argument2);
}

size_t copyDebugEvents(uint32_t afterSequence, DebugEvent *output,
                       size_t capacity,
                       DebugLogReadMetadata *metadata = nullptr) {
  TaskLockGuard lock(debugLogMutex);
  const size_t copied =
      debugLog.copyAfter(afterSequence, output, capacity, metadata);
  if (metadata != nullptr) {
    metadata->serialDropped = serialLogDroppedCount();
    metadata->serialTruncated = serialLogTruncatedCount();
  }
  return copied;
}

void copyTaskProfiler(TaskProfilerSnapshot &output) {
  taskProfiler.copySnapshot(output);
  loopPhaseProfiler.copySnapshot(output.loopPhases);
}

void copyControlStatus(ControlStatusSnapshot &output) {
  TaskLockGuard lock(controlStatusMutex);
  output = publishedControlStatus;
  output.snapshotStale =
      __atomic_load_n(&controlStatusPublishRequested, __ATOMIC_ACQUIRE);
}

void copyControlGate(ControlGateSnapshot &output) {
  TaskLockGuard lock(controlGateMutex);
  output = publishedControlGate;
}


void reportTaskWatchdogFault() {
  safetyEventFlags.set(SAFETY_EVENT_CRITICAL_TASK_WATCHDOG);
}

bool criticalTaskWatchdogFaulted() {
  return safetyEventFlags.isSet(SAFETY_EVENT_CRITICAL_TASK_WATCHDOG);
}

void feedOrTripCurrentTaskWatchdog() {
  if (!feedCurrentTaskWatchdog()) {
    reportTaskWatchdogFault();
  }
}

void requestSafeRestart() {
  safetyEventFlags.set(SAFETY_EVENT_SAFE_RESTART);
}

bool safeRestartPending() {
  return safetyEventFlags.isSet(SAFETY_EVENT_SAFE_RESTART);
}

bool consumeSafeRestartRequest() {
  return safetyEventFlags.consume(SAFETY_EVENT_SAFE_RESTART);
}

#ifndef SHOT_STOPPER_HOST_TEST
// The Arduino core would otherwise cancel the bootloader rollback inside
// initArduino(), before this firmware has proven it can do anything. Deferring
// hands that decision to ShotStopperNetwork, which confirms the image only
// after it has served its Web UI.
extern "C" bool verifyRollbackLater() {
  return true;
}
#endif

size_t copyShotRecords(ShotLogRecord *output, size_t capacity) {
  TaskLockGuard lock(shotStoreMutex);
  return shotLog.copyNewestFirst(output, capacity);
}

size_t copyShotCurves(ShotCurveRecord *output, size_t capacity) {
  TaskLockGuard lock(shotStoreMutex);
  return shotCurves.copyNewestFirst(output, capacity);
}

void copyHistoryPage(HistoryPage &page, size_t offset, size_t limit,
                     ShotLogSortDir dir) {
  TaskLockGuard lock(shotStoreMutex);
  historyLog.copyPage(page, offset, limit, dir);
}

bool deleteHistoryRecord(uint32_t id) {
  TaskLockGuard lock(shotStoreMutex);
  const bool changed = historyLog.removeById(id, false);
  if (changed) shotStoreDirtyGeneration.fetch_add(1, std::memory_order_release);
  return changed;
}

bool clearHistoryLog() {
  TaskLockGuard lock(shotStoreMutex);
  const bool changed = historyLog.clear(false);
  if (changed) shotStoreDirtyGeneration.fetch_add(1, std::memory_order_release);
  return changed;
}

uint32_t copyShotLogBootId() {
  TaskLockGuard lock(shotStoreMutex);
  return shotLog.bootId();
}

bool copyShotStoreStatus(uint32_t &bootId, PersistedLastShot &last,
                         PersistedLastShot &good, ShotCurveRecord &goodCurve,
                         bool includeGoodHistory) {
  TaskLockGuard lock(shotStoreMutex);
  bootId = shotLog.bootId();
  last = persistedLastShot;
  good = persistedLastGoodShot;
  good.rating = 0;
  goodCurve = emptyShotCurveRecord();
  uint8_t rating = 0;
  if (!includeGoodHistory || !publishableLastShot(good) ||
      good.shotLogId == 0 ||
      !shotLog.copyRatingById(good.shotLogId, rating))
    return false;
  good.rating = rating;
  (void)shotCurves.copyByShotId(good.shotLogId, goodCurve);
  return true;
}

bool deleteShotRecord(uint32_t id) {
  TaskLockGuard lock(shotStoreMutex);
  const bool hadCurve = shotCurves.containsShotId(id);
  const bool hadLog = shotLog.containsId(id);
  if (!hadLog && !hadCurve) {
    return false;
  }
  if (hadCurve && !shotCurves.removeById(id, false)) {
    return false;
  }
  if (hadLog && !shotLog.removeById(id, false)) {
    return false;
  }
  shotStoreDirtyGeneration.fetch_add(1, std::memory_order_release);
  return true;
}

bool clearShotLog() {
  TaskLockGuard lock(shotStoreMutex);
  if (!shotCurves.clear(false)) {
    return false;
  }
  if (!shotLog.clear(false)) return false;
  shotStoreDirtyGeneration.fetch_add(1, std::memory_order_release);
  return true;
}

bool clearLastShot() {
  TaskLockGuard lock(shotStoreMutex);
  if (!lastShotStore.clearLast(false)) return false;
  lastShotNvsDirty = true;
  shotStoreDirtyGeneration.fetch_add(1, std::memory_order_release);
  return true;
}

void clearLastShotRuntimeState() {
  lastShotNvsDirty = false;
}

#ifndef SHOT_STOPPER_HOST_TEST
bool resetAllDurableStoresForNetwork(PersistedSettings &settings) {
  TaskLockGuard lock(shotStoreMutex);
  if (!resetAllDurableStores(settings, bleScanPersistedSettings, shotLog,
                             historyLog, lastShotStore, shotCurves)) {
    return false;
  }
  // Drop transient dirty state only after the durable factory reset succeeds.
  // A staged scan setting must not survive the reset's default write.
  clearLastShotRuntimeState();
  bleScanPersistMux.lock();
  bleScanPersistPending = false;
  bleScanBackoffPersistPending = false;
  bleScanBoostPersistPending = false;
  pendingBleScanRequestIdCount = 0;
  bleScanPersistFailLatched = false;
  bleScanPersistMux.unlock();
  return true;
}

bool releaseNvsSpaceForFactoryResetForNetwork() {
  TaskLockGuard lock(shotStoreMutex);
  return releaseNvsSpaceForFactoryReset(lastShotStore);
}
#endif

void persistLastShotSnapshot(const PersistedLastShot &snapshot) {
  TaskLockGuard lock(shotStoreMutex);
  lastShotStore.advance(snapshot, runtimeConfig.bbwProtectionMs);
  lastShotNvsDirty = true;
  shotStoreDirtyGeneration.fetch_add(1, std::memory_order_relaxed);
}

bool rateShotRecord(uint32_t id, uint8_t rating) {
  TaskLockGuard lock(shotStoreMutex);
  if (id == 0 || rating > SHOT_LOG_RATING_MAX) {
    return false;
  }
  const bool changed = shotLog.updateRating(id, rating, false);
  if (changed) shotStoreDirtyGeneration.fetch_add(1, std::memory_order_release);
  return changed;
}

bool rateLastShot(uint8_t rating) {
  TaskLockGuard lock(shotStoreMutex);
  if (rating > SHOT_LOG_RATING_MAX || !persistedLastGoodShot.valid ||
      persistedLastGoodShot.shotLogId == 0) {
    return false;
  }
  const bool changed =
      shotLog.updateRating(persistedLastGoodShot.shotLogId, rating, false);
  if (changed) shotStoreDirtyGeneration.fetch_add(1, std::memory_order_release);
  return changed;
}

void persistLastShotFromFinalize(const PendingShotFinalize &snapshot,
                                 float finalWeightG, bool finalWeightValid) {
  PersistedLastShot last = {};
  last.valid = true;
  last.cycleId = snapshot.cycleId;
  last.endedAtUptimeMs = snapshot.endedAtMs;
  last.presetId = snapshot.activePresetId;
  copyCString(last.presetName, sizeof(last.presetName), snapshot.activePresetName);
  last.durationMs = static_cast<uint32_t>(snapshot.durationDs) * 100U;
  last.endReason = snapshot.endReason;
  last.weightValid = finalWeightValid;
  last.currentWeightG = finalWeightValid ? finalWeightG : 0.0f;
  last.goalWeightG = snapshot.goalWeightG;
  last.extractionExtended =
      snapshot.extractionExtended && snapshot.extractionGuardEnabled;
  last.slowExtractionExtended =
      snapshot.slowExtractionExtended && snapshot.slowExtractionGuardEnabled;
  last.activeStopWeightG =
      last.extractionExtended
          ? snapshot.maxRecoveryWeightG
          : (last.slowExtractionExtended
                 ? snapshot.minRecoveryWeightG
                 : static_cast<float>(snapshot.goalWeightG));
  if (snapshot.firstDropDs != SHOT_LOG_METRIC_MISSING) {
    last.firstDropElapsedMs =
        static_cast<uint32_t>(snapshot.firstDropDs) * 100U;
  }
  if (last.weightValid && last.firstDropElapsedMs != 0 &&
      last.durationMs > last.firstDropElapsedMs + 500U &&
      snapshot.scaleBaselineReady) {
    const float delta = last.currentWeightG - snapshot.scaleBaselineG;
    last.averageFlowGps =
        delta * 1000.0f /
        static_cast<float>(last.durationMs - last.firstDropElapsedMs);
    last.averageFlowValid =
        delta > 0.0f && std::isfinite(last.averageFlowGps);
  }
  last.retarePerformed = snapshot.retarePerformed;
  last.shotType = static_cast<uint8_t>(lastShotTypeFromCycle(
      snapshot.finalState, snapshot.startedWithScale, snapshot.timerOnly,
      snapshot.automaticBrew));
  last.scaleAvailable = snapshot.startedWithScale;
  last.fastExtractionGuardEnabled = snapshot.extractionGuardEnabled;
  last.slowExtractionGuardEnabled = snapshot.slowExtractionGuardEnabled;
  last.autoToManualGuardEnabled = snapshot.autoToManualGuardEnabled;
  last.autoToManualGuardEnforced = snapshot.autoToManualGuardEnforced;
  last.autoToManualGuardArmed = snapshot.autoToManualGuardArmed;
  last.autoToManualGuardRemainingMs = snapshot.autoToManualGuardRemainingMs;
  last.noScaleShotGuardEnabled = snapshot.noScaleShotGuardEnabled;
  last.noScaleShotGuardArmed = snapshot.noScaleShotGuardArmed;
  last.noScaleBbwMode = snapshot.noScaleBbwMode;
  copyCString(last.scaleProtocol, sizeof(last.scaleProtocol),
              snapshot.scaleProtocol);
  last.scaleProtocol[sizeof(last.scaleProtocol) - 1] = '\0';
  if (last.extractionExtended) {
    last.minBbwBrewTimeRemainingMs =
        last.durationMs >= snapshot.minBbwBrewTimeMs
            ? 0U
            : snapshot.minBbwBrewTimeMs - last.durationMs;
  }
  persistLastShotSnapshot(last);
}

// Weight counts as registered for the ended cycle only when the scale
// delivered a finite reading after the cycle started. Shared by the persisted
// last shot, lastCycle, and the activation-history no-weight flag.
bool endedCycleWeightValid() {
  return currentWeightSequence != session.weightSequenceAtStart &&
         std::isfinite(currentWeight) &&
         static_cast<int32_t>(currentWeightReceivedAtMs - session.startedAtMs) >=
             0;
}

void persistLastShotFromEndedCycle(EndReason reason, uint32_t durationMs) {
  PersistedLastShot last = {};
  last.valid = true;
  last.cycleId = session.id;
  last.endedAtUptimeMs = millis();
  last.presetId = session.activePresetId;
  copyCString(last.presetName, sizeof(last.presetName), session.activePresetName);
  last.durationMs = durationMs;
  last.endReason = reason;
  last.weightValid = endedCycleWeightValid();
  last.currentWeightG = last.weightValid ? currentWeight : 0.0f;
  const bool lastAcceptedValid =
      session.hasWeightAnchor && std::isfinite(session.lastAcceptedWeightG);
  if (lastAcceptedValid &&
      (!last.weightValid ||
       !plausibleSettledBrewWeight(last.currentWeightG,
                                   session.lastAcceptedWeightG, true))) {
    last.weightValid = true;
    last.currentWeightG = session.lastAcceptedWeightG;
  }
  last.goalWeightG = session.config.goalWeightG;
  last.extractionExtended =
      session.extractionExtended && session.config.fastExtractionGuardEnabled;
  last.slowExtractionExtended =
      session.slowExtractionExtended &&
      session.config.slowExtractionGuardEnabled;
  last.activeStopWeightG =
      last.extractionExtended
          ? session.config.maxRecoveryWeightG
          : (last.slowExtractionExtended
                 ? session.config.minRecoveryWeightG
                 : static_cast<float>(session.config.goalWeightG));
  const uint32_t startMs =
      session.circuitClosedAtMs != 0U ? session.circuitClosedAtMs : session.startedAtMs;
  if (session.firstDropMs != 0 &&
      static_cast<int32_t>(session.firstDropMs - startMs) >= 0) {
    last.firstDropElapsedMs = session.firstDropMs - startMs;
  }
  last.retarePerformed = session.retarePerformed;
  last.shotType = static_cast<uint8_t>(lastShotTypeFromCycle(
      stopperState, session.startedWithScale, session.config.timerOnly,
      shot.automaticBrew));
  last.scaleAvailable = session.startedWithScale;
  last.fastExtractionGuardEnabled = session.config.fastExtractionGuardEnabled;
  last.slowExtractionGuardEnabled = session.config.slowExtractionGuardEnabled;
  last.autoToManualGuardEnabled = session.config.autoToManualGuardEnabled;
  last.autoToManualGuardEnforced = session.autoToManualGuardEnforced;
  last.autoToManualGuardArmed = session.autoToManualGuardArmed;
  last.noScaleShotGuardEnabled = noScaleBbwEnabled(runtimeConfig.noScaleBbwMode);
  last.noScaleShotGuardArmed = noScaleShotGuardArmed;
  last.noScaleBbwMode = runtimeConfig.noScaleBbwMode;
  if (session.autoToManualGuardEnforced) {
    const uint32_t nowMs = millis();
    last.autoToManualGuardRemainingMs =
        static_cast<int32_t>(session.autoToManualGuardDeadlineAtMs - nowMs) <=
                0
            ? 0U
            : session.autoToManualGuardDeadlineAtMs - nowMs;
  }
  if (last.extractionExtended) {
    last.minBbwBrewTimeRemainingMs =
        durationMs >= session.config.minBbwBrewTimeMs
            ? 0U
            : session.config.minBbwBrewTimeMs - durationMs;
  }
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  copyCString(last.scaleProtocol, sizeof(last.scaleProtocol),
              link.protocolName);
  last.scaleProtocol[sizeof(last.scaleProtocol) - 1] = '\0';
  persistLastShotSnapshot(last);
}

bool controlAllowsConfigurationNow();
RuntimeConfig effectiveRuntimeConfig();
void resetCupPresence();

bool enqueueWebCommand(const WebCommand &command) {
  return webCommandQueue != nullptr &&
         xQueueSend(webCommandQueue, &command, 0) == pdTRUE;
}

RuntimeConfig effectiveRuntimeConfig() {
  return composeEffectiveConfig(runtimeConfig, presetBank);
}

void publishRecipeState() {
  TaskLockGuard lock(recipeMutex);
  publishedRuntimeConfig = runtimeConfig;
  publishedPresetBank = presetBank;
}

void requestActivePresetMachineTemperature() {
  const ShotPreset &preset = activeShotPreset(presetBank);
  requestMachineIntegrationPresetTemperature(
      preset.id, runtimeConfig.revision, preset.lineaMicraBrewTargetDeciC);
}

void copyRecipeSnapshot(RecipeSnapshot *out) {
  if (out == nullptr) {
    return;
  }
  TaskLockGuard lock(recipeMutex);
  out->presets = publishedPresetBank;
  out->runtime = publishedRuntimeConfig;
}

void copyPresetBank(ShotPresetBank *out) {
  RecipeSnapshot snapshot;
  copyRecipeSnapshot(&snapshot);
  if (out != nullptr) *out = snapshot.presets;
}

void copyRuntimeConfig(RuntimeConfig *out) {
  RecipeSnapshot snapshot;
  copyRecipeSnapshot(&snapshot);
  if (out != nullptr) *out = snapshot.runtime;
}

void copyBullseyeConfig(BullseyeMelodyConfig *out) {
  if (out == nullptr) {
    return;
  }
  bullseyeConfigMux.lock();
  *out = bullseyeMelodyConfig;
  bullseyeConfigMux.unlock();
}

bool stageBullseyeConfig(const BullseyeMelodyConfig &config,
                         uint32_t requestId) {
  if (requestId == 0 || !validBullseyeMelodyConfig(config)) {
    return false;
  }
  bullseyeConfigMux.lock();
  stagedBullseyeMelodyConfig = config;
  stagedBullseyeRequestId = requestId;
  bullseyeConfigMux.unlock();
  return true;
}

bool takeStagedBullseyeConfig(uint32_t requestId,
                              BullseyeMelodyConfig &out) {
  bool matched = false;
  bullseyeConfigMux.lock();
  if (requestId != 0 && stagedBullseyeRequestId == requestId) {
    out = stagedBullseyeMelodyConfig;
    stagedBullseyeRequestId = 0;
    matched = true;
  }
  bullseyeConfigMux.unlock();
  return matched;
}

void commitLiveBullseyeConfig(const BullseyeMelodyConfig &config) {
  bullseyeConfigMux.lock();
  bullseyeMelodyConfig = config;
  bullseyeConfigMux.unlock();
  (void)localBuzzer.configureBullseyeRtttl(bullseyeMelodyConfig.rtttl);
  if (!bullseyeMelodyConfig.enabled) {
    bullseyeTracker.clear();
  }
#ifndef SHOT_STOPPER_HOST_TEST
  networkManager.syncLiveBullseye(bullseyeMelodyConfig);
#endif
}


bool scaleAvailable() {
  return scaleLinkAvailable(getScaleLinkSnapshot());
}


uint32_t controlLoopTickDelayMs() {
  return scaleAvailable() ? 1 : LOOP_NO_SCALE_DELAY_MS;
}

bool weightStreamIsLive(WeightStreamState state) {
  return state == WeightStreamState::FRESH ||
         state == WeightStreamState::OVERLOAD ||
         state == WeightStreamState::ANOMALOUS;
}

void noteRecoverableStaleTransition(WeightStreamState previous,
                                    WeightStreamState next,
                                    const ScaleLinkSnapshot &link,
                                    uint32_t now) {
  if (recoverableStaleOpen) {
    const bool sameLink =
        link.state == ScaleLinkState::CONNECTED &&
        link.disconnectSequence == recoverableStaleDisconnectSequence &&
        link.connectionGeneration == recoverableStaleConnectionGeneration;
    if (!sameLink) {
      recoverableStaleOpen = false;
    } else if (weightStreamIsLive(next)) {
      ++scaleRecoveredStaleCount;
      scaleRecoveredStaleMs +=
          static_cast<uint32_t>(now - recoverableStaleStartedAtMs);
      recoverableStaleOpen = false;
    }
  }
  if (!recoverableStaleOpen && next == WeightStreamState::STALE &&
      weightStreamIsLive(previous) &&
      link.state == ScaleLinkState::CONNECTED &&
      observedWeightConnectionGeneration == link.connectionGeneration) {
    recoverableStaleOpen = true;
    recoverableStaleStartedAtMs = now;
    recoverableStaleDisconnectSequence = link.disconnectSequence;
    recoverableStaleConnectionGeneration = link.connectionGeneration;
  }
}

bool currentWeightIsFresh(uint32_t now = millis(),
                          const ScaleLinkSnapshot &link = getScaleLinkSnapshot()) {
  const uint32_t linkGeneration = link.connectionGeneration;
  return currentWeightSequence > 0 && std::isfinite(currentWeight) &&
         currentWeight >= MIN_AUTOMATION_WEIGHT_G &&
         currentWeight <= MAX_AUTOMATION_WEIGHT_G &&
         (currentWeightConnectionGeneration == 0 ||
          currentWeightConnectionGeneration == linkGeneration) &&
         static_cast<int32_t>(now - currentWeightReceivedAtMs) >= 0 &&
         static_cast<uint32_t>(now - currentWeightReceivedAtMs) <=
             MAX_AUTOMATION_WEIGHT_AGE_MS;
}

bool observedWeightIsFresh(uint32_t now = millis(),
                           const ScaleLinkSnapshot &link = getScaleLinkSnapshot()) {
  const uint32_t linkGeneration = link.connectionGeneration;
  return observedWeightSequence > 0 && std::isfinite(observedWeight) &&
         fabsf(observedWeight) <= MAX_PARSED_WEIGHT_G &&
         observedWeightConnectionGeneration != 0 &&
         observedWeightConnectionGeneration == linkGeneration &&
         static_cast<int32_t>(now - observedWeightReceivedAtMs) >= 0 &&
         static_cast<uint32_t>(now - observedWeightReceivedAtMs) <=
             MAX_AUTOMATION_WEIGHT_AGE_MS;
}

WeightStreamState observedWeightStreamStateAt(uint32_t now,
                                              const ScaleLinkSnapshot &link) {
  if (observedWeightSequence == 0) {
    return WeightStreamState::NO_SAMPLE;
  }
  if (!observedWeightIsFresh(now, link)) {
    return WeightStreamState::STALE;
  }
  return weightStreamState;
}

void serviceWeightStreamTelemetry(const ScaleLinkSnapshot &link) {
  const uint32_t now = millis();
  const WeightStreamState next = observedWeightStreamStateAt(now, link);
  noteRecoverableStaleTransition(telemetryWeightStreamState, next, link, now);
  telemetryWeightStreamState = next;
}

void latchAtmCurveFromSession(uint32_t atMs) {
  if (session.hasWeightAnchor && std::isfinite(session.lastAcceptedWeightG)) {
    shotCurveSampler.latchAtm(atMs, session.lastAcceptedWeightG);
  }
}

void setWeightControlState(WeightControlState state) {
  if (session.weightControlState == state) {
    session.automaticEnabled = state == WeightControlState::ACTIVE;
    return;
  }
  const WeightControlState previous = session.weightControlState;
  session.weightControlState = state;
  session.automaticEnabled = state == WeightControlState::ACTIVE;
  if (state == WeightControlState::SUSPENDED) {
    session.scaleWasLost = true;
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::SCALE_CONTROL_SUSPENDED,
                  static_cast<int32_t>(previous));
    if (session.autoToManualGuardArmed &&
        !session.autoToManualGuardEnforced) {
      session.autoToManualGuardEnforced = true;
      latchAtmCurveFromSession(millis());
      addDebugEvent(DebugCategory::SCALE,
                    DebugCode::AUTO_TO_MANUAL_GUARD_ENFORCED,
                    static_cast<int32_t>(
                        elapsedMs(session.startedAtMs)),
                    static_cast<int32_t>(
                        session.autoToManualGuardDeadlineAtMs -
                        session.startedAtMs));
    }
  } else if (state == WeightControlState::ACTIVE &&
             (previous == WeightControlState::SUSPENDED ||
              previous == WeightControlState::VALIDATING)) {
    addDebugEvent(DebugCategory::SCALE,
                  DebugCode::SCALE_CONTROL_RECOVERED);
    if (session.autoToManualGuardEnforced) {
      session.autoToManualGuardEnforced = false;
      shotCurveSampler.latchAtmCleared(millis());
      addDebugEvent(DebugCategory::SCALE,
                    DebugCode::AUTO_TO_MANUAL_GUARD_CLEARED);
    }
  }
}

void resetAccidentalTouchState() {
  session.accidentalTouchPhase = AccidentalTouchPhase::STARTUP;
  session.accidentalTouchClass = AccidentalTouchClass::OK;
  session.accidentalTouchHolding = false;
  session.accidentalTouchPendingCount = 0;
}

void resetWeightTrend() {
  shot.expectedEndS = session.config.operationalWallMs / 1000.0f;
  shot.datapoints = 0;
  resetAccidentalTouchState();
}

void suspendWeightControl() {
  if (session.weightControlState == WeightControlState::ACTIVE ||
      session.weightControlState == WeightControlState::VALIDATING) {
    setWeightControlState(WeightControlState::SUSPENDED);
    session.recoveryConfirmations = 0;
    session.calibrationEligible = false;
    resetWeightTrend();
  }
}

bool scaleAutomationUnavailableForSession() {
  const ScaleLinkSnapshot snapshot = getScaleLinkSnapshot();
  return !scaleLinkAvailable(snapshot) ||
         !observedWeightIsFresh() ||
         snapshot.disconnectSequence !=
             session.scaleDisconnectSequenceAtStart;
}


RelaySafetySnapshot getRelaySafetySnapshot();
bool machineRunningElapsed(uint32_t &elapsedOut);
uint32_t machineElapsedMs();
bool machineIsRunning();
bool machineRequestStart(uint32_t operationalLimitMs,
                         bool remoteActuation = false);
bool machineRequestStop();
bool machineRequestWebStop();
bool machineRequestForcedPulse();
void requestRemoteTimerStop();
uint32_t cycleShotElapsedMs();

float cycleElapsedSeconds() {
  return cycleShotElapsedMs() / 1000.0f;
}

uint32_t cycleShotElapsedMs() {
  if (!session.active) {
    return 0U;
  }
  return machineElapsedMs();
}

void captureCycleCircuitStart() {
  const RelaySafetySnapshot startedRelay = getRelaySafetySnapshot();
  session.circuitClosedAtMs =
      startedRelay.closed ? startedRelay.closedAtMs : session.startedAtMs;
}

uint32_t endedCycleDurationMs() {
  uint32_t durationMs = machineElapsedMs();
  if (durationMs != 0U) {
    return durationMs;
  }
  const uint32_t startMs = session.circuitClosedAtMs != 0U
                               ? session.circuitClosedAtMs
                               : session.startedAtMs;
  return startMs != 0U ? elapsedMs(startMs) : 0U;
}

void flushPendingScaleTimerStopNow() {
  if (!pendingScaleTimerStop.pending) {
    return;
  }
  pendingScaleTimerStop = PendingScaleTimerStop{};
  requestRemoteTimerStop();
}

uint32_t scaleTimerDisplayTargetMs(uint32_t internalMs) {
  return (internalMs / 1000U) * 1000U;
}

void completePendingScaleTimerStop() {
  pendingScaleTimerStop = PendingScaleTimerStop{};
  requestRemoteTimerStop();
}

void armScaleTimerStopExtraDelay(uint32_t extraDelayMs) {
  if (extraDelayMs == 0U) {
    completePendingScaleTimerStop();
    return;
  }
  pendingScaleTimerStop.pending = true;
  pendingScaleTimerStop.extraDueAtMs = millis() + extraDelayMs;
}

void servicePendingScaleTimerStop() {
  if (!pendingScaleTimerStop.pending) {
    return;
  }
  const uint32_t nowMs = millis();
  if (pendingScaleTimerStop.extraDueAtMs != 0U) {
    if (static_cast<int32_t>(nowMs - pendingScaleTimerStop.extraDueAtMs) >= 0 ||
        !scaleAvailable()) {
      completePendingScaleTimerStop();
    }
    return;
  }
  if (!scaleAvailable()) {
    completePendingScaleTimerStop();
    return;
  }
  const bool timedOut =
      static_cast<int32_t>(nowMs -
                           pendingScaleTimerStop.catchupDeadlineAtMs) >= 0;
  if (!session.remoteTimerStartSettled && !timedOut) {
    return;
  }
  const uint32_t extraDelayMs = pendingScaleTimerStop.extraDelayMs;
  if (session.remoteTimerStartSettled && !session.remoteTimerStarted) {
    armScaleTimerStopExtraDelay(extraDelayMs);
    return;
  }
  const ScaleLinkSnapshot link = getScaleLinkSnapshot();
  if (timedOut || !link.timerValid ||
      link.timerMs >= pendingScaleTimerStop.targetMs) {
    armScaleTimerStopExtraDelay(extraDelayMs);
  }
}

void scheduleScaleTimerStopAfterCycle(uint32_t internalElapsedMs) {
  if (!session.timerStartCommandQueued || session.stopTimerRequested) {
    return;
  }
  pendingScaleTimerStop = PendingScaleTimerStop{};
  pendingScaleTimerStop.pending = true;
  pendingScaleTimerStop.targetMs = scaleTimerDisplayTargetMs(internalElapsedMs);
  pendingScaleTimerStop.extraDelayMs = session.config.scaleTimerStopExtraDelayMs;
  pendingScaleTimerStop.catchupDeadlineAtMs =
      millis() + MAX_SCALE_TIMER_STOP_CATCHUP_MS;
  servicePendingScaleTimerStop();
}

void transitionTo(StopperState nextState) {
  if (stopperState == nextState) {
    return;
  }

  const StopperState previousState = stopperState;
  stopperState = nextState;
  addDebugEvent(DebugCategory::STATE, DebugCode::STATE_TRANSITION,
                static_cast<int32_t>(previousState),
                static_cast<int32_t>(nextState));
}

void initializeScaleConnectedLed() {
  if (!SCALE_STATUS_LED_PRESENT) {
    scaleConnectedLedInitialized = false;
    return;
  }
  pinMode(SCALE_CONNECTED_LED_GPIO, OUTPUT);
  digitalWrite(SCALE_CONNECTED_LED_GPIO,
               SCALE_CONNECTED_LED_INACTIVE_LEVEL);
  lastScaleConnectedLedOn = false;
  lastScaleConnectedLedPattern = ScaleConnectedLedPattern::OFF;
  lastScaleConnectedLedToggleAtMs = 0;
  scaleConnectedLedInitialized = true;
}

ScaleConnectedLedPattern desiredScaleConnectedLedPattern(
    const ScaleLinkSnapshot &link) {
  if (!runtimeConfig.scaleConnectedLed) {
    return ScaleConnectedLedPattern::OFF;
  }
  if (link.state == ScaleLinkState::CONNECTED) {
    if (observedWeightStreamStateAt(millis(), link) == WeightStreamState::STALE) {
      return ScaleConnectedLedPattern::SLOW_BLINK;
    }
    return ScaleConnectedLedPattern::SOLID;
  }
  if (link.connecting) {
    return ScaleConnectedLedPattern::FAST_BLINK;
  }
  return ScaleConnectedLedPattern::OFF;
}

void serviceScaleConnectedLed(const ScaleLinkSnapshot &link = getScaleLinkSnapshot()) {
  if (!SCALE_STATUS_LED_PRESENT) {
    return;
  }
  const ScaleConnectedLedPattern pattern = desiredScaleConnectedLedPattern(link);
  const uint32_t now = millis();
  bool on = false;
  uint32_t periodMs = 0;
  switch (pattern) {
    case ScaleConnectedLedPattern::SOLID:
      on = true;
      break;
    case ScaleConnectedLedPattern::FAST_BLINK:
      periodMs = SCALE_LED_FAST_BLINK_MS;
      break;
    case ScaleConnectedLedPattern::SLOW_BLINK:
      periodMs = SCALE_LED_SLOW_BLINK_MS;
      break;
    case ScaleConnectedLedPattern::OFF:
    default:
      on = false;
      break;
  }

  if (periodMs != 0) {
    if (!scaleConnectedLedInitialized ||
        pattern != lastScaleConnectedLedPattern) {
      on = true;
      lastScaleConnectedLedToggleAtMs = now;
    } else if (elapsedMs(lastScaleConnectedLedToggleAtMs) >= periodMs) {
      on = !lastScaleConnectedLedOn;
      lastScaleConnectedLedToggleAtMs = now;
    } else {
      on = lastScaleConnectedLedOn;
    }
  }

  if (scaleConnectedLedInitialized && on == lastScaleConnectedLedOn &&
      pattern == lastScaleConnectedLedPattern) {
    return;
  }
  digitalWrite(SCALE_CONNECTED_LED_GPIO,
               on ? SCALE_CONNECTED_LED_ACTIVE_LEVEL
                  : SCALE_CONNECTED_LED_INACTIVE_LEVEL);
  lastScaleConnectedLedOn = on;
  lastScaleConnectedLedPattern = pattern;
  scaleConnectedLedInitialized = true;
}

AlertOutputChannel currentAlertOutputChannel() {
  return effectiveAlertOutputChannel(runtimeConfig.alertOutputChannel);
}

bool soundAlertsEnabled() { return !runtimeConfig.soundAlertsMuted; }

// These policy decisions execute only on the control task. The worker receives
// a concrete volume command and never reads runtimeConfig.
void requestBookooSilenceIfConfigured() {
  if (!soundAlertsEnabled() ||
      (runtimeConfig.bookooMuteOnBuzzerOnly &&
       currentAlertOutputChannel() == AlertOutputChannel::BUZZER_ONLY)) {
    (void)enqueueScaleDebugCommand(BookooDebugAction::VOLUME, 0);
  }
}

void requestBookooAlertVolumeRestore() {
  const AlertOutputChannel channel = currentAlertOutputChannel();
  if (soundAlertsEnabled() && runtimeConfig.bookooConnectBeepLevel >= 1 &&
      runtimeConfig.bookooConnectBeepLevel <= BOOKOO_BEEP_LEVEL_MAX &&
      (channel == AlertOutputChannel::SCALE_ONLY ||
       channel == AlertOutputChannel::SCALE_PRIORITY)) {
    (void)enqueueScaleDebugCommand(BookooDebugAction::VOLUME,
                                   runtimeConfig.bookooConnectBeepLevel);
  }
}

#ifndef SHOT_STOPPER_HOST_TEST
void serviceBootRecoverySafety();
#endif

#include "ShotStopperMachine.h"

StopperState nextStateForUserHold(const MachineIntention &intent) {
  return intent.holdActive ? StopperState::REQUIRES_OFF : StopperState::READY;
}

void servicePendingBrewRfRestore() {
  if (!pendingBrewRfRestore) {
    return;
  }
  pendingBrewRfRestore = false;
}

// ---------------------------------------------------------------------------

// Behavior-preserving service fragments; kept in this translation unit.
void appendHistoryRecord(HistoryType type, uint32_t durationMs,
                         bool noWeight);

#include "platform/ShotStopperPowerRuntime.inc"
#include "control/ShotStopperWakeGesture.inc"
#include "control/ShotStopperCycleRuntime.inc"
#include "scale/ShotStopperScaleEvents.inc"
#include "control/ShotStopperControlStateMachine.inc"
#include "persistence/ShotStopperCommandPersistence.inc"
#include "control/ShotStopperCommands.inc"
#include "diagnostics/ShotStopperDiagnostics.inc"
#include "platform/ShotStopperEntrypoints.inc"
