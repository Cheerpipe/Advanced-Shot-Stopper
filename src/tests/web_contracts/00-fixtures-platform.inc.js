'use strict';

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const crypto = require('crypto');
const webUi = require('../../scripts/gen_web_ui.js');
const imageTag = require('../../scripts/image_tag.js');

const sketchDir = path.resolve(__dirname, '..');
function readSources(files) {
  return files.map((file) => fs.readFileSync(path.join(sketchDir, file), 'utf8'))
    .join('\n');
}
const asset = fs.readFileSync(path.join(sketchDir, 'ShotStopperWebAssets.h'), 'utf8');
const network = readSources([
  'ShotStopperNetwork.cpp',
  'network/ShotStopperNetworkService.inc',
  'network/ShotStopperWifi.inc',
  'network/ShotStopperHttpLifecycle.inc',
  'network/ShotStopperHttpAuthAssets.inc',
  'network/ShotStopperStatus.inc',
  'diagnostics/ShotStopperNetworkDiagnostics.inc',
  'network/ShotStopperConfiguration.inc',
  'network/ShotStopperNetworkOta.inc',
]);
const networkHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperNetwork.h'), 'utf8');
const otaSource = fs.readFileSync(path.join(sketchDir, 'ShotStopperOta.cpp'), 'utf8');
const otaHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperOta.h'), 'utf8');
const webhookSource = fs.readFileSync(path.join(sketchDir, 'ShotStopperWebhook.cpp'), 'utf8');
const webhookHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperWebhook.h'), 'utf8');
const firmwareCore = readSources([
  'shotStopper.cpp',
  'control/ShotStopperCycleRuntime.inc',
  'scale/ShotStopperScaleEvents.inc',
  'control/ShotStopperControlStateMachine.inc',
  'persistence/ShotStopperCommandPersistence.inc',
  'control/ShotStopperCommands.inc',
  'diagnostics/ShotStopperDiagnostics.inc',
  'platform/ShotStopperEntrypoints.inc',
]);
const idfMain = fs.readFileSync(
  path.resolve(sketchDir, '..', 'idf', 'main', 'main.cpp'), 'utf8');
const scaleWorker = fs.readFileSync(path.join(sketchDir, 'ShotStopperScaleWorker.cpp'), 'utf8');
const firmware = [
  firmwareCore,
  scaleWorker,
  fs.readFileSync(path.join(sketchDir, 'ShotStopperScaleWorker.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperScaleLink.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperHardware.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachine.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperRfCoex.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineRelay.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineActivatorSample.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleInput.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleControl.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleState.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddlePolicy.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineMomentaryInput.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineMomentaryConfig.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineMomentaryControl.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineMomentaryReedState.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineMomentaryOnlyState.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperScaleSense.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperCupPresence.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperRinse.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperShotCurveTypes.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperAlert.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperAlertChannel.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperAlertTone.h'), 'utf8'),
].join('\n');
const shotLogIo = fs.readFileSync(path.join(sketchDir, 'ShotStopperShotLog.h'), 'utf8');
const shotCurveIo = fs.readFileSync(path.join(sketchDir, 'ShotStopperShotCurve.h'), 'utf8');
const lastShotIo = fs.readFileSync(path.join(sketchDir, 'ShotStopperLastShot.h'), 'utf8');
const jsonArena = fs.readFileSync(path.join(sketchDir, 'ShotStopperJsonArena.h'), 'utf8');
const wallClock = fs.readFileSync(path.join(sketchDir, 'ShotStopperTime.h'), 'utf8');
const domainCore = fs.readFileSync(path.join(sketchDir, 'ShotStopperDomain.h'), 'utf8');
const domain = [
  domainCore,
  fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperScaleTypes.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ShotStopperBrewTypes.h'), 'utf8'),
].join('\n');
const serialCli = fs.readFileSync(path.join(sketchDir, 'ShotStopperSerialCli.h'), 'utf8');
const buzzer = fs.readFileSync(path.join(sketchDir, 'ShotStopperBuzzer.h'), 'utf8');
const buzzerPatterns = fs.readFileSync(path.join(sketchDir, 'ShotStopperBuzzerPatterns.h'), 'utf8');
const buzzerPassive = fs.readFileSync(path.join(sketchDir, 'ShotStopperBuzzerPassive.h'), 'utf8');
const rtttlParser = fs.readFileSync(path.join(sketchDir, 'ShotStopperRtttl.h'), 'utf8');
const buzzerRtttl = fs.readFileSync(path.join(sketchDir, 'ShotStopperBuzzerRtttl.h'), 'utf8');
const alertChannel = fs.readFileSync(path.join(sketchDir, 'ShotStopperAlertChannel.h'), 'utf8');
const alertTone = fs.readFileSync(path.join(sketchDir, 'ShotStopperAlertTone.h'), 'utf8');
const kconfig = fs.readFileSync(
  path.resolve(sketchDir, '..', 'idf', 'main', 'Kconfig.projbuild'), 'utf8');
const bleSrcDir = path.resolve(sketchDir, '..', 'libraries', 'EspressoScaleBLE', 'src');
function readTree(dir) {
  return fs.readdirSync(dir, { withFileTypes: true }).map((entry) => {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      return readTree(full);
    }
    if (/\.(cpp|h)$/.test(entry.name)) {
      return fs.readFileSync(full, 'utf8');
    }
    return '';
  }).join('\n');
}
const bleLibrary = readTree(bleSrcDir);
const bleCompanion = [
  fs.readFileSync(path.join(sketchDir, 'ShotStopperBleCompanion.h'), 'utf8'),
  fs.readFileSync(path.join(sketchDir, 'ble',
                            'ShotStopperBleCompanionNimble.cpp'), 'utf8'),
].join('\n');
const taskProfiler = fs.readFileSync(
  path.join(sketchDir, 'ShotStopperTaskProfiler.h'), 'utf8');
const sdkconfigDefaults = fs.readFileSync(
  path.resolve(sketchDir, '..', 'idf', 'sdkconfig.defaults'), 'utf8');
const sdkconfigNimble = fs.readFileSync(
  path.resolve(sketchDir, '..', 'idf', 'sdkconfig.defaults.nimble'), 'utf8');
const bleComponentCmake = fs.readFileSync(
  path.resolve(sketchDir, '..', 'idf', 'components', 'EspressoScaleBLE',
               'CMakeLists.txt'), 'utf8');
const idfBuildScript = fs.readFileSync(
  path.resolve(sketchDir, '..', 'scripts', 'build-idf'), 'utf8');
const bleRuntime = fs.readFileSync(
  path.resolve(sketchDir, '..', 'idf', 'components',
               'ShotStopperBleRuntime', 'ShotStopperBleRuntime.cpp'),
  'utf8');
const flashIoScratch = fs.readFileSync(
  path.join(sketchDir, 'ShotStopperFlashIoScratch.h'), 'utf8');
if (!idfMain.includes('bool bleInUse(void) { return true; }')) {
  throw new Error(
      'Native NimBLE must keep BLE controller memory from being released by initArduino');
}
if (!bleCompanion.includes('ble_gatts_add_svcs(services)') ||
    !bleCompanion.includes('os_mbuf_append(context->om') ||
    bleCompanion.includes('BLECharacteristic') ||
    bleCompanion.includes('ArduinoBLE')) {
  throw new Error('BLE Companion must use native NimBLE GATTS APIs only');
}
if (!taskProfiler.includes('allocExternal(sizeof(ActiveWorkspace))') ||
    !taskProfiler.includes('heapCapsFree(workspace_)') ||
    taskProfiler.includes('allocExternalOrInternal(sizeof(ActiveWorkspace))') ||
    taskProfiler.includes('calloc(') ||
    /\bfree\(workspace_\)/.test(taskProfiler) ||
    /\bfree\(next\)/.test(taskProfiler)) {
  throw new Error(
      'TaskProfiler workspace must use allocExternal/heapCapsFree, not calloc/free or internal fallback');
}
if (taskProfiler.includes('task.xCoreID') ||
    !taskProfiler.includes('xTaskGetCoreID(task.xHandle)')) {
  throw new Error(
      'TaskProfiler must read core pin via xTaskGetCoreID, not TaskStatus_t.xCoreID');
}
if (!sdkconfigDefaults.includes('CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y') ||
    !sdkconfigDefaults.includes('CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID=y')) {
  throw new Error(
      'sdkconfig.defaults must enable run-time stats and vTaskList core IDs');
}
if (!sdkconfigNimble.includes('CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y') ||
    !sdkconfigNimble.includes('CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096') ||
    !sdkconfigNimble.includes('CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=96') ||
    !sdkconfigNimble.includes('CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES=4') ||
    !sdkconfigNimble.includes('CONFIG_BT_NIMBLE_GATT_MAX_PROCS=2') ||
    !sdkconfigNimble.includes('CONFIG_BT_NIMBLE_MAX_CCCDS=4')) {
  throw new Error(
      'Production NimBLE defaults must retain the qualified pools, stack, MTU and external allocator');
}
if (!bleComponentCmake.includes('EspressoScaleBLENimble.cpp') ||
    bleComponentCmake.includes('ESPRESSO_SCALE_BLE_BACKEND') ||
    bleComponentCmake.includes('ArduinoBLE') ||
    idfBuildScript.includes('SHOTSTOPPER_BLE_BACKEND') ||
    idfBuildScript.includes('SHOTSTOPPER_NIMBLE_ALLOCATOR') ||
    fs.existsSync(path.resolve(sketchDir, '..', 'scripts',
                               'patch_arduinoble.sh')) ||
    fs.existsSync(path.resolve(sketchDir, '..', 'idf', 'components',
                               'ArduinoBLE')) ||
    fs.existsSync(path.resolve(sketchDir, '..', 'idf',
                               'sdkconfig.defaults.arduinoble')) ||
    fs.existsSync(path.resolve(sketchDir, '..', 'idf',
                               'sdkconfig.defaults.nimble-internal'))) {
  throw new Error(
      'Production must build native NimBLE only, without backend selectors or ArduinoBLE infrastructure');
}
for (const service of [
  'PROX', 'ANS', 'CTS', 'HTP', 'IPSS', 'TPS',
  'IAS', 'LLS', 'SPS', 'HR', 'BAS', 'DIS'
]) {
  if (!sdkconfigNimble.includes(
          `# CONFIG_BT_NIMBLE_${service}_SERVICE is not set`)) {
    throw new Error(`Unused NimBLE ${service} service must stay disabled`);
  }
}
for (const feature of ['DTM_MODE_TEST', 'SM_SIGN_CNT', 'CPFD_CAFD']) {
  if (!sdkconfigNimble.includes(
          `# CONFIG_BT_NIMBLE_${feature} is not set`)) {
    throw new Error(`Unused NimBLE ${feature} feature must stay disabled`);
  }
}
if (!bleRuntime.includes('gHostTask = xTaskGetCurrentTaskHandle()') ||
    !bleRuntime.includes('uxTaskGetStackHighWaterMark(') ||
    !bleRuntime.includes('hostTaskHandle')) {
  throw new Error(
      'NimBLE HEALTH must sample the live host-task stack watermark');
}
if (!sdkconfigDefaults.includes('CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768') ||
    !sdkconfigDefaults.includes('xTaskCreate still allocates internal stacks') ||
    sdkconfigDefaults.includes('CONFIG_FREERTOS_PLACE_TASK_STACKS_IN_EXT_RAM=y')) {
  throw new Error(
      'sdkconfig.defaults must pin SPIRAM_MALLOC_RESERVE_INTERNAL=32768 and keep task stacks internal');
}
if (!sdkconfigDefaults.includes('CONFIG_FREERTOS_USE_TICKLESS_IDLE=y') ||
    sdkconfigDefaults.includes('CONFIG_PM_ENABLE=y')) {
  throw new Error(
      'sdkconfig.defaults must enable tickless idle without CONFIG_PM / light sleep');
}
if (sdkconfigDefaults.includes('CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y') ||
    !sdkconfigDefaults.includes('CONFIG_ESP_CONSOLE_NONE=y') ||
    !sdkconfigDefaults.includes('CONFIG_ESP_CONSOLE_SECONDARY_NONE=y') ||
    !sdkconfigDefaults.includes('CONFIG_SHOT_STOPPER_ENABLE_JTAG=0')) {
  throw new Error(
      'IDF console must default to NONE; JTAG stays off unless ENABLE_JTAG=1');
}
{
  const sdkconfigJtag = fs.readFileSync(
      path.resolve(sketchDir, '..', 'idf', 'sdkconfig.defaults.jtag'), 'utf8');
  if (!sdkconfigJtag.includes('CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y') ||
      !sdkconfigJtag.includes('CONFIG_SHOT_STOPPER_ENABLE_JTAG=1') ||
      !sdkconfigJtag.includes('# CONFIG_ESP_CONSOLE_NONE is not set')) {
    throw new Error(
        'sdkconfig.defaults.jtag must turn USB Serial/JTAG on for ENABLE_JTAG=1');
  }
}
if (sdkconfigDefaults.includes('CONFIG_BT_LE_SLEEP_ENABLE=y') ||
    sdkconfigDefaults.includes('CONFIG_BT_CTRL_MODEM_SLEEP=y')) {
  throw new Error(
      'BT controller modem-sleep must stay off (would apply during GATT)');
}
{
  const cmakeLists = fs.readFileSync(
      path.resolve(sketchDir, '..', 'idf', 'CMakeLists.txt'), 'utf8');
  const boardSh = fs.readFileSync(
      path.resolve(sketchDir, '..', 'scripts', 'shotstopper_board.sh'), 'utf8');
  const bleHeader = fs.readFileSync(
      path.resolve(sketchDir, '..', 'libraries', 'EspressoScaleBLE', 'src',
                   'EspressoScaleBLE.h'),
      'utf8');
  if (!cmakeLists.includes('ARDUINO_USB_CDC_ON_BOOT=${_ss_cdc_on_boot}') ||
      !cmakeLists.includes('set(_ss_cdc_on_boot 0)') ||
      !cmakeLists.includes('-DSHOT_STOPPER_ENABLE_JTAG=1') ||
      cmakeLists.includes('COMPILE_DEFINITIONS "ARDUINO_USB_CDC_ON_BOOT=0"') ||
      cmakeLists.includes('COMPILE_DEFINITIONS "ARDUINO_USB_CDC_ON_BOOT=1"')) {
    throw new Error(
        'IDF CMakeLists must default Arduino CDC off and turn it on only for ENABLE_JTAG=1');
  }
  if (boardSh.includes('CDCOnBoot=cdc') ||
      !boardSh.includes('CDCOnBoot=default')) {
    throw new Error(
        'Legacy FQBN must use CDCOnBoot=default to match jumper-gated CDC');
  }
  if (!bleHeader.includes('#define BLE_SCAN_LIGHT_INTERVAL           0x00B8') ||
      !bleHeader.includes('#define BLE_SCAN_LIGHT_WINDOW             0x002E') ||
      !bleHeader.includes('#define BLE_SCAN_NORMAL_INTERVAL          0x0064') ||
      !bleHeader.includes('#define BLE_SCAN_NORMAL_WINDOW            0x0032') ||
      !bleHeader.includes('#define BLE_SCAN_AGGRESSIVE_INTERVAL      0x0020') ||
      !bleHeader.includes('#define BLE_SCAN_AGGRESSIVE_WINDOW        0x0020')) {
    throw new Error(
        'BLE scan presets must be Light 25% (28.75/115), Normal 50% (31.25/62.5), Aggressive 100% (20/20)');
  }
  if (!firmware.includes('USB_CONSOLE_GPIO') ||
      !firmware.includes('SHOT_STOPPER_USB_CONSOLE_GPIO 4') ||
      !firmwareCore.includes('usbConsoleJumperPresent()') ||
      !firmwareCore.includes('UsbSerialEnableSource::COMPILE_FLAG') ||
      !firmwareCore.includes('UsbSerialEnableSource::JUMPER') ||
      !firmwareCore.includes('Serial.begin(SERIAL_BAUD)') ||
      !firmwareCore.includes(
          'usbSerialEnableSource = UsbSerialEnableSource::COMPILE_FLAG') ||
      !firmwareCore.includes(
          'usbSerialEnableSource = UsbSerialEnableSource::JUMPER')) {
    throw new Error(
        'USB CDC must start for ENABLE_JTAG=1 or when the GPIO4 jumper is held at boot');
  }
  const usbConsole = fs.readFileSync(
      path.join(sketchDir, 'ShotStopperUsbConsole.h'), 'utf8');
  const serialCli = fs.readFileSync(
      path.join(sketchDir, 'ShotStopperSerialCli.h'), 'utf8');
  if (!serialCli.includes('#include "ShotStopperUsbConsole.h"') ||
      !usbConsole.includes('SHOT_STOPPER_USB_CONSOLE_OWN_HWCDC') ||
      !usbConsole.includes('#define Serial ::shotStopperUsbConsole') ||
      !firmwareCore.includes('HWCDC shotStopperUsbConsole')) {
    throw new Error(
        'GPIO4 jumper must start USB Serial/JTAG HWCDC, not UART0 Serial0');
  }
  const buildIdf = fs.readFileSync(
      path.resolve(sketchDir, '..', 'scripts', 'build-idf'), 'utf8');
  const idfHelpers = fs.readFileSync(
      path.resolve(sketchDir, '..', 'scripts', 'shotstopper_idf.sh'), 'utf8');
  if (!idfHelpers.includes('ss_idf_prepare_set_target') ||
      !idfHelpers.includes('ss_idf_commit_extra_flags_stamp') ||
      !idfHelpers.includes('ss_idf_jtag_enabled') ||
      !idfHelpers.includes('ss_idf_sync_jtag_console') ||
      !idfHelpers.includes('sdkconfig.defaults.jtag') ||
      !buildIdf.includes('ss_idf_prepare_set_target') ||
      !buildIdf.includes('ss_idf_commit_extra_flags_stamp') ||
      !buildIdf.includes('ss_idf_sync_jtag_console') ||
      !idfHelpers.includes('Preparing empty IDF build tree for set-target')) {
    throw new Error(
        'build-idf must empty a non-CMake tree before idf.py set-target fullclean');
  }
}
if (!taskProfiler.includes('void copySnapshot(TaskProfilerSnapshot &out) const') ||
    !taskProfiler.includes('TaskLockGuard lock(reportMutex_)') ||
    taskProfiler.includes('__atomic_load_n(&seq_') ||
    !taskProfiler.includes('beginSnapshotWrite_()')) {
  throw new Error(
      'TaskProfiler snapshot copies must use a task mutex; uxTaskGetSystemState must not run under it');
}
if (!firmwareCore.includes('#include "ShotStopperTaskProfiler.h"') ||
    !firmwareCore.includes('TaskProfiler taskProfiler') ||
    !firmwareCore.includes('taskProfiler.service(millis())') ||
    !firmwareCore.includes('void copyTaskProfiler(TaskProfilerSnapshot &output)') ||
    !firmwareCore.includes('taskProfiler.copySnapshot(output)') ||
    !firmwareCore.includes('WebCommandType::TASK_PROFILER_START') ||
    !firmwareCore.includes('WebCommandType::TASK_PROFILER_STOP') ||
    !firmwareCore.includes('callbacks.copyTaskProfiler = copyTaskProfiler') ||
    (firmwareCore.match(/taskProfiler\.start\(/g) || []).length !== 1) {
  throw new Error(
      'TaskProfiler must be wired as opt-in Diagnostic telemetry with copyTaskProfiler');
}
{
  const setupBody = firmwareCore.slice(
      firmwareCore.indexOf('void setup()'), firmwareCore.indexOf('void loop()'));
  const publishBody = firmwareCore.slice(
      firmwareCore.indexOf('void publishControlStatus()'),
      firmwareCore.indexOf('void resetSerialCliState()'));
  if (setupBody.includes('taskProfiler.start') ||
      publishBody.includes('taskProfiler') ||
      publishBody.includes('TaskProfiler')) {
    throw new Error(
        'Task profiler must not auto-start on boot and must not ride ControlStatusSnapshot');
  }
}
if (!network.includes('copyTaskProfiler') ||
    !network.includes('statusJsonAppendTaskProfiler') ||
    !network.includes('\\"tasks\\"') ||
    !network.includes('\\"currentTotalCpuPct\\"') ||
    !network.includes('\\"unreportedCurrentCpuPct\\"') ||
    !network.includes('/api/v1/diagnostic/profiler') ||
    !network.includes('taskProfilerHandler') ||
    !network.includes('static constexpr size_t kStatusJson = 12288') ||
    !networkHeader.includes('void (*copyTaskProfiler)(TaskProfilerSnapshot &out)') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperDebugExport.h'), 'utf8')
        .includes('DEBUG_EXPORT_SCHEMA_VERSION = 6')) {
  throw new Error(
      'Diagnostic status, POST /api/v1/diagnostic/profiler, and debug export must expose tasks');
}
const psram = fs.readFileSync(path.join(sketchDir, 'ShotStopperPsram.h'), 'utf8');
if (!psram.includes('#define SHOT_STOPPER_PSRAM_BSS EXT_RAM_BSS_ATTR') ||
    !firmwareCore.includes('SHOT_STOPPER_PSRAM_BSS ShotLog shotLog') ||
    !firmwareCore.includes('SHOT_STOPPER_PSRAM_BSS ShotCurveLog shotCurves') ||
    !firmwareCore.includes(
        'SHOT_STOPPER_PSRAM_BSS PersistedSettings persistedSettings') ||
    !firmwareCore.includes(
        'SHOT_STOPPER_PSRAM_BSS SettingsPersistRequest settingsPersistRequest') ||
    !firmwareCore.includes(
        'SHOT_STOPPER_PSRAM_BSS SettingsPersistRequest settingsPersistReceive') ||
    !firmwareCore.includes(
        'xQueueReceive(settingsPersistQueue, &settingsPersistReceive') ||
    !firmwareCore.includes(
        'constexpr uint32_t SETTINGS_PERSIST_TASK_STACK_SIZE = 4096') ||
    !network.includes(
        'static SHOT_STOPPER_PSRAM_BSS WifiScanSnapshot g_wifiScan') ||
    !network.includes(
        'static SHOT_STOPPER_PSRAM_BSS PersistedSettings g_networkSettings') ||
    !networkHeader.includes('PersistedSettings &settings_') ||
    !firmwareCore.includes(
        'SHOT_STOPPER_PSRAM_BSS DebugRingBuffer debugLog') ||
    !firmwareCore.includes(
        'SHOT_STOPPER_PSRAM_BSS RuntimeConfig publishedRuntimeConfig') ||
    !firmwareCore.includes(
        'SHOT_STOPPER_PSRAM_BSS ShotPresetBank publishedPresetBank') ||
    firmwareCore.includes('stagingControlStatus') ||
    !flashIoScratch.includes('allocInternal(FLASH_IO_SCRATCH_BYTES)') ||
    firmwareCore.includes('SHOT_STOPPER_PSRAM_BSS ControlStatusSnapshot')) {
  throw new Error(
      'Large history/settings/debug-ring/recipe BSS must use SHOT_STOPPER_PSRAM_BSS; flash scratch and live status snapshots stay internal');
}
if (!buzzer.includes('struct RtttlCatalog') ||
    !buzzer.includes('RtttlCatalog *rtttlCatalog') ||
    !buzzer.includes('allocInternal(sizeof(RtttlCatalog))') ||
    !buzzer.includes('RtttlNote rtttlBuf[BULLSEYE_RTTTL_MAX_NOTES]') ||
    !firmwareCore.includes('TaskMutex debugLogMutex') ||
    !firmwareCore.includes(
        'debugLog.copyAfter(afterSequence, output, capacity)') ||
    firmwareCore.includes('portENTER_CRITICAL(&debugLogMux)') ||
    !firmwareCore.includes('debugLogDroppedSnapshot') ||
    !firmwareCore.includes(
        '__atomic_load_n(&debugLogContentionDropped, __ATOMIC_RELAXED)') ||
    firmwareCore.includes('__atomic_add_fetch(&debugLogDroppedSnapshot')) {
  throw new Error(
      'RTTTL catalog and active playback buffer must stay internal RAM — playback starts from the esp_timer task under a spinlock, and PSRAM is unreachable while flash writes disable the cache');
}
if (network.includes('ControlStatusSnapshot status;') ||
    network.includes('ControlStatusSnapshot control;') ||
    firmwareCore.includes('ControlStatusSnapshot status;') ||
    !network.includes('ControlGateSnapshot ShotStopperNetwork::controlGate()') ||
    !firmwareCore.includes('void copyControlGate(ControlGateSnapshot &output)')) {
  throw new Error(
      'Network/httpd gate checks must copy ControlGateSnapshot, not a stack ControlStatusSnapshot');
}
if (firmwareCore.includes('portENTER_CRITICAL(&webStatusMux)') ||
    firmwareCore.includes('next.presets = presetBank') ||
    firmwareCore.includes('copyScaleHistory(next.scaleHistory)') ||
    !firmwareCore.includes('TaskMutex controlStatusMutex') ||
    !firmwareCore.includes('TaskMutex controlGateMutex') ||
    !firmwareCore.includes('TaskMutex recipeMutex') ||
    firmwareCore.includes('controlStatusSeq') ||
    firmwareCore.includes('controlGateSeq') ||
    firmwareCore.includes('recipeSeq') ||
    !network.includes('self.callbacks_.copyPresetBank(&g_work->presetBank)') ||
    !network.includes('self.callbacks_.copyScaleHistory(g_work->scaleHistory)')) {
  throw new Error(
      'Status and recipe snapshots must use task mutexes and fill presets/history on demand');
}
if (network.includes('composeEffectiveConfig(candidate, status.presets)') ||
    network.includes('status.presets') ||
    !network.includes('self.callbacks_.copyPresetBank(&livePresets)') ||
    !network.includes('composeEffectiveConfig(candidate, livePresets)')) {
  throw new Error(
      'configHandler must composeEffectiveConfig against copyPresetBank, not ControlGateSnapshot.presets');
}
if (!flashIoScratch.includes('inline void feedFlashIoWatchdog()') ||
    !flashIoScratch.includes('esp_task_wdt_status(nullptr) == ESP_OK') ||
    !flashIoScratch.includes('(void)esp_task_wdt_reset()')) {
  throw new Error(
      'Flash I/O watchdog feed must reset TWDT only when the current task is subscribed');
}
if (!bleLibrary.includes('length == 0 || length > MAX_BLE_PACKET_LENGTH') ||
    !bleLibrary.includes('os_mbuf_copydata(buffer, 0, length, frame.data)')) {
  throw new Error('Native NimBLE reads must bound and copy mbuf payloads');
}
if (!bleLibrary.includes('params.filter_duplicates = 0') ||
    !bleLibrary.includes('const bool useAddressScan = filtered && addressScan') ||
    !bleLibrary.includes('nimbleAdvertisementIsCompatible') ||
    !bleLibrary.includes('scaleUuid16AllowsNamelessConnect') ||
    !firmware.includes(
        'currentScaleMacCacheMode() == ScaleMacCacheMode::ONLY') ||
    !firmware.includes(
        'scale.startScan(mac, forceRestart, interval, window, addressScan)')) {
  throw new Error(
      'Native GAP scan must retain duplicate reports, UUID matching and ONLY address filtering');
}
if (!firmware.includes('scaleWorkerTickDelayMs()') ||
    !firmware.includes('controlLoopTickDelayMs()') ||
    !firmware.includes('SCALE_WORKER_NO_SCALE_DELAY_MS') ||
    !firmware.includes('SCALE_STREAM_GAP_MS') ||
    !firmwareCore.includes('void refreshControlStatus()') ||
    !firmwareCore.includes('void serviceControlStatusPublish()') ||
    !firmwareCore.includes('void publishControlGate()') ||
    !firmwareCore.includes('vTaskDelay(pdMS_TO_TICKS(1))') ||
    firmwareCore.includes('CONTROL_STATUS_PUBLISH_MS') ||
    firmwareCore.includes('CONTROL_STATUS_PUBLISH_NO_SCALE_MS') ||
    (firmware.split('publishBleCompanionStatus(inactiveStatus)').length - 1) !== 1) {
  throw new Error(
      'No-scale idle must relax worker/loop; status snapshot is GET-driven, not 50 ms');
}
if (/\bruntimeConfig\b/.test(scaleWorker) ||
    /\bfirmwareInitializationComplete\b/.test(scaleWorker) ||
    /\bresetCupPresence\s*\(/.test(scaleWorker) ||
    /\bemitAlert\s*\(/.test(scaleWorker) ||
    !firmwareCore.includes('publishScaleWorkerPolicy(runtimeConfig,') ||
    !firmwareCore.includes('void processScaleLinkTransitions()')) {
  throw new Error(
      'Scale worker must consume published policy/facts; only control may mutate cup state or emit alerts');
}
{
  const loopBody = firmwareCore.slice(firmwareCore.indexOf('void loop()'));
  const drainCount = (loopBody.match(/processScaleWorkerEvents\(\)/g) || []).length;
  if (drainCount < 2 ||
      !network.includes('bool ShotStopperNetwork::lockWorkBufForStatus') ||
      !network.includes('void ShotStopperNetwork::loadControlStatus') ||
      !network.includes('callbacks_.refreshControlStatus != nullptr') ||
      (network.split('lockWorkBufForStatus()').length - 1) < 4 ||
      !firmwareCore.includes('callbacks.refreshControlStatus = refreshControlStatus') ||
      !firmwareCore.includes(
          '__atomic_store_n(&controlStatusPublishRequested, false')) {
    throw new Error(
        'Weight mailbox must drain twice per loop; GET status must wait for a committed snapshot');
  }
}
{
  const reedState = fs.readFileSync(
      path.join(sketchDir, 'ShotStopperMachineMomentaryReedState.h'), 'utf8');
  const getterNames = [
    ['bool machineAllowsFirmwareStopPulse', 'inline bool machineRunningElapsed'],
    ['inline bool machineRunningElapsed', 'inline bool machineIsRunning'],
    ['inline bool machineIsRunning', 'inline uint32_t machineElapsedMs'],
    ['inline MachineRunState machineRunState', 'inline void machineFillInferenceStatus'],
  ];
  for (const [startName, endName] of getterNames) {
    const start = reedState.indexOf(startName);
    const end = reedState.indexOf(endName);
    if (start < 0 || end <= start || reedState.slice(start, end).includes('sampleReed()')) {
      throw new Error('Reed getters must use the last sampled state, not sampleReed()');
    }
  }
  if (!reedState.includes('void serviceReedAssumeWindow() {\n  sampleReed();') ||
      !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachine.h'), 'utf8')
           .includes('sampleReed();')) {
    throw new Error('Reed must be sampled from input/service, not from getters');
  }
}
{
  const hwmon = fs.readFileSync(path.join(sketchDir, 'ShotStopperHwmon.h'), 'utf8');
  if (!hwmon.includes('const HeapCapSnapshot *heap = nullptr') ||
      !hwmon.includes('heap != nullptr ? *heap : sampleHeapCaps()') ||
      !firmwareCore.includes('hwmon.sample(intervalMs > 0U ? intervalMs') ||
      !firmwareCore.includes('&heap)')) {
    throw new Error('Hwmon must reuse the 5 s HeapCapSnapshot instead of sampling heap twice');
  }
}
if (!domain.includes('HEALTH_HEAP_LOW_RESTART_MS') ||
    !firmwareCore.includes('DebugCode::HEALTH_HEAP_RESTART') ||
    !firmwareCore.includes('controlAllowsConfigurationNow() && !circuitClosed')) {
  throw new Error(
      'Sustained heap-low must request a safe restart only from Ready with the circuit open');
}
if (!bleLibrary.includes('BLE_CONNECT_TIMEOUT_MS + kConnectCallbackMarginMs') ||
    !bleLibrary.includes('ble_gap_connect(shotStopperBleRuntimeOwnAddressType()') ||
    !bleLibrary.includes('beginOperation(CallbackDomain::Link)') ||
    !bleLibrary.includes('event.generation != generation_') ||
    !bleLibrary.includes('finishLink(true')) {
  throw new Error('Native GAP/GATT operations must be deadline- and generation-bounded');
}
if (/#define\s+BLE_CONNECT_TIMEOUT_MS\s+4000UL/.test(bleLibrary)) {
  throw new Error(
      'GAP connect timeout must stay under the 5 s task watchdog (not 4 s)');
}
if (!bleLibrary.includes(
        'return clientFromStorage(_nimbleClientStorage).isLinkUp()') ||
    !bleLibrary.includes(
        'return clientFromStorage(_nimbleClientStorage).newWeightAvailable()')) {
  throw new Error(
      'EspressoScaleBLE facade must delegate link and packet state to native NimBLE storage');
}
if (firmware.includes('if (scaleLinked || changed)')) {
  throw new Error(
      'Companion status publish must not force every tick while the scale is linked');
}
if (!firmware.includes('companionAdvertisingShouldPause') ||
    !firmware.includes('syncCompanionAdvertisingForScaleLink') ||
    !firmware.includes('scale.isConnecting()') ||
    !firmware.includes('SCALE_HUNT_RF_CLEAR_MS') ||
    !firmware.includes('scaleHuntRfClearActive') ||
    firmware.includes('BLE.poll(') ||
    !scaleWorker.includes(
        'ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(tickDelayMs))') ||
    (firmware.split('syncCompanionAdvertisingForScaleLink();').length - 1) < 3 ||
    (scaleWorker.split('syncScaleRadioCoex();').length - 1) < 2 ||
    !scaleWorker.includes('GAP/GATT setup must not wait behind') ||
    !bleCompanion.includes('!paused && status_.stackReady && !status_.connected') ||
    !bleCompanion.includes('BLE_COMPANION_ADV_INTERVAL') ||
    !bleCompanion.includes('params.itvl_min = BLE_COMPANION_ADV_INTERVAL')) {
  throw new Error(
      'Companion advertising must pause while connecting, scale-linked, or in the hunt RF window; worker must block on HCI');
}
if (firmware.includes('SCALE_LINK_COEX_BT_MS') ||
    firmware.includes('scaleLinkCoexHadLink') ||
    firmware.includes('setRfCoexClaim') ||
    firmware.includes('rfCoexWinner') ||
    firmware.includes('preferStaWifiCoex') ||
    !firmware.includes('scale.isConnecting() || scale.isLinkUp()') ||
    !firmware.includes('ensureRfCoexBt') ||
    !firmware.includes('ESP_COEX_PREFER_BT') ||
    firmware.includes('applyRfCoexPreference') ||
    firmware.includes('ESP_COEX_PREFER_WIFI') ||
    firmware.includes('ESP_COEX_PREFER_BALANCE') ||
    firmware.includes('RfCoexPreference::WIFI') ||
    firmware.includes('RfCoexPreference::BALANCE') ||
    !firmware.includes('serviceScaleScanIntensity') ||
    firmware.includes('serviceScaleScanDuty') ||
    firmware.includes('SCALE_SCAN_BURST_MS') ||
    firmware.includes('nextScaleConnectRetryMs') ||
    firmware.includes('SCALE_CONNECT_RETRY_MS') ||
    !firmware.includes('applyLiveBleScanIntensity')) {
  throw new Error(
      'RF coex must always prefer BT (ensureRfCoexBt); claims and STA WIFI preference are gone; scan uses live intensity, not idle/burst');
}
if (otaSource.includes('OTA_PROGRESS_INTERVAL_BYTES') ||
    otaSource.includes('runningPartition_') ||
    otaSource.includes('stagedSizeBytes_') ||
    otaHeader.includes('runningPartition_') ||
    otaHeader.includes('stagedSizeBytes_')) {
  throw new Error('OTA must not retain the unused F-22 progress constant or inert partition/size state');
}
{
  const restoreStart = firmwareCore.indexOf('void servicePendingBrewRfRestore()');
  const restoreEnd = firmwareCore.indexOf('\nvoid ', restoreStart + 1);
  const restore = restoreStart >= 0 && restoreEnd > restoreStart
      ? firmwareCore.slice(restoreStart, restoreEnd)
      : '';
  if (restore.includes('applyBrewRfPreference(false)')) {
    throw new Error(
        'servicePendingBrewRfRestore must not force BALANCE; syncScaleRadioCoex owns the BLE claim');
  }
}
if (/setScaleLinkState\(ScaleLinkState::CONNECTED\);\s*applyBookooConnectBeepPolicy/.test(
        firmwareCore)) {
  throw new Error(
      'Bookoo connect volume must be deferred off the GATT success path');
}
if (!network.includes('\\"lastDisconnectReasonName\\":\\"%s\\"},') ||
    !network.includes('\\"macCachePauseRemainingMs\\":%lu,')) {
  throw new Error(
      'Home status must include lastDisconnectReasonName on scale');
}

const htmlMatch = asset.match(/R"HTML\(([\s\S]*?)\)HTML"/);
