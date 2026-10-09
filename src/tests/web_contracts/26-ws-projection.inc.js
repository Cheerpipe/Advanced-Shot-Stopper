// Execute the production serializers: omitted metrics cannot trigger a live
// object delta, but the full debug export must keep those measurements.
{
  const assert = require('assert').strict, {spawnSync} = require('child_process');
  const read = file => fs.readFileSync(path.join(sketchDir, file), 'utf8');
  const networkSource = read('ShotStopperNetwork.cpp');
  const body = (source, signature) => {
    const start = source.indexOf(signature);
    assert(start >= 0, signature);
    return source.slice(start, source.indexOf('\n}', start) + 2);
  };
  const native = `
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include "ShotStopperDomain.h"
#include "ShotStopperTaskProfiler.h"
#include "ShotStopperScaleProfiler.h"
#include "ShotStopperShotCurveTypes.h"
using namespace shotstopper;
struct NetworkWorkBuf {
  static constexpr size_t kStatusJson=40960,kJsonItem=28672;
  char statusJson[kStatusJson]{},jsonItem[kJsonItem]{},curveJson[SHOT_CURVE_JSON_CAPACITY]{};
  ShotCurveRecord serializedCurve{};
} work;
auto *g_work=&work;
${body(networkSource, 'void sanitizeJsonEmbed(')}
${body(networkSource, 'bool jsonScratchAppend(')}
${body(networkSource, 'bool __attribute__((format(printf, 2, 3)))\nstatusJsonAppend(')}
${body(networkSource, 'bool formatTaskProfilerObject(')}
${body(networkSource, 'bool statusJsonAppendScaleProfiler(')}
struct ShotStopperNetwork {
  static size_t formatShotStatsRow(NetworkWorkBuf &,const ShotLogRecord &,const ShotCurveRecord *);
};
${body(read('network/ShotStopperHomeStream.inc'), 'size_t ShotStopperNetwork::formatShotStatsRow(')}
int main(){
  TaskProfilerSnapshot tasks;
  tasks.rowCount=1;strcpy(tasks.rows[0].name,"worker");tasks.rows[0].stackMinBytes=1024;
  tasks.loopPhases.rowCount=LOOP_PHASE_COUNT;
  for(auto &row:tasks.loopPhases.rows){row.name="test";row.sampleCount=5;row.recentGapExecutionUs=12;}
  auto format=[&](bool full){size_t used=0;assert(formatTaskProfilerObject(work.statusJson,sizeof(work.statusJson),&used,tasks,full));return std::string(work.statusJson);};
  const auto live=format(false),full=format(true);
  puts(live.c_str());puts(full.c_str());
  tasks.remainingMs=900;tasks.sampleCount=20;tasks.averageTotalCpuPct=80;
  tasks.lastCaptureUs=100;tasks.maxCaptureUs=200;
  tasks.micraBackflushWaitLastUs=11;tasks.micraBackflushWaitMaxUs=22;
  tasks.scaleEventsWaitLastUs=33;tasks.scaleEventsWaitMaxUs=44;
  tasks.shotStoreWaitLastUs=55;tasks.shotStoreWaitMaxUs=66;
  tasks.controlStatusWaitLastUs=77;tasks.controlStatusWaitMaxUs=88;
  for(auto &row:tasks.loopPhases.rows)row.maxCpuUs=7;
  for(auto &row:tasks.loopPhases.rows){row.averageExecutionUs=100;row.maxExecutionUs=1000;row.lastExecutionUs=500;}
  assert(format(false)==live && format(true)!=full);
  const auto detailed=format(true);
  assert(detailed.find("\\\"maxCpuUs\\\":7")!=std::string::npos);
  assert(detailed.find("\\\"lockWaits\\\":{\\\"micraBackflush\\\":{\\\"lastUs\\\":11,"
                      "\\\"maxUs\\\":22},\\\"scaleEvents\\\":{\\\"lastUs\\\":33,"
                      "\\\"maxUs\\\":44},\\\"shotStore\\\":{\\\"lastUs\\\":55,"
                      "\\\"maxUs\\\":66},\\\"controlStatus\\\":{\\\"lastUs\\\":77,"
                      "\\\"maxUs\\\":88}}")!=std::string::npos);
  ++tasks.loopPhases.rows[0].recentGapExecutionUs;
  assert(format(false)!=live);
  size_t used=0;assert(!formatTaskProfilerObject(work.statusJson,40,&used,tasks,false));
  ScaleProfilerStatus profile;profile.partitionAvailable=true;
  auto scale=[&](){size_t used=0;assert(statusJsonAppendScaleProfiler(&used,profile));return std::string("{")+work.statusJson+"}";};
  const auto empty=scale();puts(empty.c_str());
  ++profile.generation;++profile.sessionId;
  assert(scale()==empty);
  profile.downloading=true;assert(scale()!=empty);puts(scale().c_str());
  profile.downloading=false;profile.lastError=ScaleProfilerError::ALLOCATION;puts(scale().c_str());
  ShotLogRecord record{};record.id=1;record.bootId=9;record.goalWeightG=40;record.actualWeightCg=4200;
  record.errorCg=200;record.avgFlowCgS=SHOT_LOG_METRIC_MISSING;
  record.firstDropDs=record.tareAtDs=SHOT_LOG_METRIC_MISSING;
  assert(ShotStopperNetwork::formatShotStatsRow(work,record,nullptr));
  const std::string row=work.jsonItem;puts(row.c_str());
  record.errorCg=500;
  assert(ShotStopperNetwork::formatShotStatsRow(work,record,nullptr));
  assert(row==work.jsonItem);
}
`;
  const directory = path.resolve(sketchDir, '..', 'temp', 'ai_temp_ws_projection_contract');
  fs.mkdirSync(directory, {recursive: true});
  const binary = path.join(directory, 'projection-' + process.pid);
  const compiled = spawnSync('c++', ['-std=c++17', '-DSHOT_STOPPER_HOST_TEST',
    '-I' + sketchDir, '-x', 'c++', '-', '-o', binary], {input: native, encoding: 'utf8'});
  assert.equal(compiled.status, 0, compiled.stderr);
  const run = spawnSync(binary, [], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stderr);
  fs.unlinkSync(binary);
  const wire = run.stdout.trim().split('\n');
  const [live, full, empty, busy, failed, row] = wire.map(JSON.parse);
  for (const key of ['stackUnit','remainingMs','sampleCount','averageTotalCpuPct','lastCaptureUs','maxCaptureUs']) {
    assert(!(key in live), key);assert(key in full, 'debug export retains '+key);
  }
  for (const key of ['averageExecutionUs','maxExecutionUs','lastExecutionUs']) {
    assert(!(key in live.rows[1]), key);assert(key in full.rows[1]);
  }
  assert.equal(live.rows[1].sampleCount, 5);
  assert.equal(live.rows[1].recentGapExecutionUs, 12);
  assert.equal(live.rows[0].stackMinWords, 1024);
  assert(Buffer.byteLength(JSON.stringify(full))-Buffer.byteLength(JSON.stringify(live)) >= 1000);
  for (const key of ['generation','sessionId','durationLimitMs','recordBytes','complete','downloading'])
    assert(!(key in empty.scaleProfile), key);
  assert.equal(empty.scaleProfile.canStart, true);
  assert.equal(busy.scaleProfile.canStart, false);
  assert.equal(failed.scaleProfile.lastError, 'allocation');
  assert.equal(row.bootId, 9);assert.equal(row.actualG, 42);
  assert(!('errorG' in row) && !('errorPct' in row));
  assert(Array.isArray(row.wCg) && Array.isArray(row.wAtMs));
  fs.writeFileSync(path.join(directory, 'sizes.json'), JSON.stringify({
    fixture: 'one task and nineteen loop phases; one synthetic 42g shot',
    taskLiveBytes: Buffer.byteLength(wire[0]),
    taskFullBytes: Buffer.byteLength(wire[1]),
    shotRowBytes: Buffer.byteLength(wire[5])}, null, 2)+'\n');
}

// Actual presentation consumers must distinguish unavailable measurements and
// the firmware's lowercase stop reason. Zero remains a legitimate NVS value.
{
  const assert = require('assert').strict, vm = require('vm');
  const source = fs.readFileSync(path.join(sketchDir, 'web/js/runtime.js'), 'utf8');
  const section = (a, b) => source.slice(source.indexOf(a), source.indexOf(b, source.indexOf(a)));
  const nodes = new Map(), $ = id => {
    if (!nodes.has(id)) nodes.set(id, {textContent: ''});return nodes.get(id);
  };
  const context = {$, __WEBUI_TEXT__: key => key, syncAdminSessionUi(){}, diagnosticPublicView:false,
    updH(){},ntpStateLabel(){},formatDiagnosticTimezoneOffset(){},renderDiagClock(){}};
  vm.createContext(context);
  vm.runInContext(section('function applyDiagnosticStatus(', 'function applyAdminStatus('), context);
  const status = {adminUnlocked:true,health:{},safety:{},maintenance:{},network:{},time:{},scale:{},
    config:{},nvs:{statsValid:false,usedEntries:0,availableEntries:20,freeEntries:0,totalEntries:20,namespaces:0}};
  context.applyDiagnosticStatus(status);
  for (const id of ['hNvsEntries','hNvsTotal','hNvsNamespaces']) assert.equal($(id).textContent, 'runtime.unknown');
  status.nvs.statsValid=true;context.applyDiagnosticStatus(status);
  assert.equal($('hNvsNamespaces').textContent,'0');assert($('hNvsEntries').textContent.startsWith('0'));
  const cup={__WEBUI_TEXT__:key=>key,$:()=>null,result:{},setHomeSub:(id,value)=>cup.result[id]=value,
    formatNoScaleGuard(){},formatExtractionGuard(){},formatAccidentalTouch(){},formatSlowExtractionGuard(){},formatAtmGuard(){}};
  vm.createContext(cup);
  vm.runInContext(section('function formatCupProtection(', 'function setHomeSub(')+
    section('function updateHomeGuardSubs(', 'function renderShotHero('),cup);
  const home={config:{brewByWeight:true,cupProtectionEnabled:true},lastShot:{valid:true,endReason:'cup_removed'},scale:{},cupPresence:{}};
  cup.updateHomeGuardSubs(home,false);assert.equal(cup.result.homeCupSub,'runtime.shot_aborted');
  home.lastShot.endReason='target';cup.updateHomeGuardSubs(home,false);assert.equal(cup.result.homeCupSub,'runtime.brew_allowed');
}
