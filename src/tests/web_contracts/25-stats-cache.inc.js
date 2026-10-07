// Exercise the production Stats sender with paced rows and observable store
// captures. Session/page transitions must neither recopy immutable data nor
// combine an old header with a new store generation.
{
  const assert = require('assert').strict, {spawnSync} = require('child_process');
  const read = (file) => fs.readFileSync(path.join(sketchDir, file), 'utf8');
  const source = read('network/ShotStopperHomeStream.inc');
  const networkSource = read('ShotStopperNetwork.cpp');
  const header = read('ShotStopperNetwork.h');
  const body = (text, signature) => {
    const start = text.indexOf(signature);
    assert(start >= 0, signature);
    return text.slice(start, text.indexOf('\n}', start) + 2);
  };
  const sessionStart = header.indexOf('  struct UiStreamSession {');
  const session = header.slice(sessionStart, header.indexOf('\n  };', sessionStart) + 5);
  const native = `
#include <cassert>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include "ShotStopperDomain.h"
#include "ShotStopperHistoryTypes.h"
#include "ShotStopperShotCurveTypes.h"
using namespace shotstopper;
constexpr int ESP_OK=0,HTTPD_WS_TYPE_TEXT=1;
constexpr size_t kStatsStreamFrameBytes=20480;
constexpr uint32_t kStatsStreamPaceMs=100;
const char *uiStreamSendKind="";
bool uiStreamSendCleanAbort=false;
uint32_t now=1,storeEpoch=1; unsigned captures=0,hashes=0; int sendResult=ESP_OK;
uint32_t millis(){return now;}
struct TaskLockGuard {explicit TaskLockGuard(int){}};
struct httpd_ws_frame_t{int type;uint8_t *payload;size_t len;};
std::vector<std::string> frames;
int httpd_ws_send_frame_async(int,int,httpd_ws_frame_t *frame){
  if(sendResult==ESP_OK)frames.emplace_back(reinterpret_cast<char *>(frame->payload),frame->len);
  return sendResult;
}
${body(networkSource, 'struct StatsStreamCache {')};
struct NetworkWorkBuf {
  static constexpr size_t kStatusJson=40960,kJsonItem=16000;
  char statusJson[kStatusJson]{},jsonItem[kJsonItem]{};
  StatsStreamCache statsCache;ShotCurveRecord serializedCurve{};
} work;
auto *g_work=&work;
${body(networkSource, 'bool __attribute__((format(printf, 2, 3)))\nstatusJsonAppend(')}
bool buildIntegrationStats(const ShotStatsView &stats,char *out,size_t cap){
  snprintf(out,cap,"{\\"shotCount\\":%u}",stats.shotCount);return true;
}
uint32_t uiStreamCurveHash(const ShotCurveRecord &curve,uint16_t count){
  ++hashes;return count ? curve.weightCg[0] : 0;
}
size_t formatShotStatsRow(NetworkWorkBuf &buffer,const ShotLogRecord &record,
                         const ShotCurveRecord *curves,size_t count){
  auto *curve=findShotCurveById(curves,count,record.id);
  assert(curve && curve->weightCg[0]==record.actualWeightCg);
  return snprintf(buffer.jsonItem,sizeof(buffer.jsonItem),
      "{\\"id\\":%u,\\"weight\\":%u,\\"padding\\":\\"%015000d\\"}",record.id,record.actualWeightCg,0);
}
void capture(ShotStatsSnapshot &out){
  ++captures;
  out.epoch=storeEpoch;out.stats.shotCount=storeEpoch;out.count=out.curveCount=3;
  for(size_t i=0;i<out.count;++i){
    out.records[i]={};out.records[i].id=i+1;out.records[i].actualWeightCg=storeEpoch*100+i;
    resetShotCurveRecord(out.curves[i]);out.curves[i].shotId=i+1;out.curves[i].count=1;
    out.curves[i].weightCg[0]=out.records[i].actualWeightCg;
  }
}
struct ShotStopperNetwork {
  static constexpr size_t WEB_UI_CLIENT_ID_CAPACITY=25;
${session}
  int dataMux_=0,server_=1;bool webUiOverrideActive_=false;uint32_t webUiOverrideUntilMs_=0;
  char activeWebUiClientId_[WEB_UI_CLIENT_ID_CAPACITY]="owner";
  std::atomic<bool> uiStreamUrgent_{false};
  struct {
    uint32_t (*shotLogEpoch)()=+[](){return storeEpoch;};
    void (*copyShotStatsSnapshot)(ShotStatsSnapshot &)=capture;
  } callbacks_;
  bool appendRecordPageUi(const ControlStatusSnapshot &,size_t *used,bool){
    return statusJsonAppend(used,"\\"ui\\":{}");
  }
  bool sendStatsStream(UiStreamSession &,const ControlStatusSnapshot &);
};
${body(source, 'bool ShotStopperNetwork::sendStatsStream(')}
int main(){
  ShotStopperNetwork network;ShotStopperNetwork::UiStreamSession session;
  strcpy(session.clientId,"owner");session.fd=1;session.statsOn=true;
  ControlStatusSnapshot control;control.bootId=9;
  auto send=[&](auto &target){now+=kStatsStreamPaceMs;return network.sendStatsStream(target,control);};
  assert(send(session)&&session.statsPaging&&session.statsSent==1);
  assert(captures==1&&hashes==3);
  assert(send(session)&&session.statsSent==2);
  assert(send(session)&&!session.statsPaging);
  assert(captures==1&&hashes==3&&frames.size()==3);
  assert(frames[0].find("\\"stats\\":{\\"shotCount\\":1}")!=std::string::npos);
  assert(frames[1].find("\\"rowBase\\":1")!=std::string::npos);
  assert(send(session)&&frames.size()==3);
  // New window/order reuses the store capture, but hashes that page anew.
  session.statsFetch=true;session.statsFetchRequest=2;session.statsFetchOffset=1;
  session.statsFetchLimit=1;session.statsFetchDir=ShotLogSortDir::Asc;
  assert(send(session)&&captures==1&&hashes==4);
  assert(!session.statsFetch&&frames.back().find("\\"id\\":2")!=std::string::npos);
  // A debug export can borrow records: invalidation must refill the cache.
  work.statsCache.valid=false;work.statsCache.snapshot.records[0].actualWeightCg=999;
  session.statsResync=true;
  assert(send(session)&&captures==2&&session.statsSent==1);
  const uint32_t oldSeq=session.statsSeq;
  ++storeEpoch;
  assert(send(session)&&captures==3&&session.statsSeq==oldSeq+1);
  assert(session.statsSent==1&&frames.back().find("\\"shotCount\\":2")!=std::string::npos);
  // An epoch advance during capture must also restart the in-flight page.
  work.statsCache.valid=false;
  network.callbacks_.copyShotStatsSnapshot=+[](ShotStatsSnapshot &out){++storeEpoch;capture(out);};
  assert(send(session)&&session.statsSeq==oldSeq+2&&session.statsSent==1);
  assert(session.statsPageEpoch==3&&frames.back().find("\\"epoch\\":3")!=std::string::npos);
  network.callbacks_.copyShotStatsSnapshot=capture;
  const size_t delivered=frames.size();const unsigned copied=captures,hashed=hashes;
  sendResult=-1;uiStreamSendCleanAbort=true;
  assert(send(session)&&session.statsSent==1&&frames.size()==delivered);
  sendResult=ESP_OK;
  assert(send(session)&&session.statsSent==2);
  assert(captures==copied&&hashes==hashed);
  // A new session owns its cursor; immutable cache contents may be shared.
  ShotStopperNetwork::UiStreamSession fresh;strcpy(fresh.clientId,"owner");fresh.fd=2;
  assert(send(fresh)&&fresh.statsSent==1&&captures==copied);
  strcpy(fresh.clientId,"superseded");
  assert(!send(fresh));
}
`;
  const directory = path.resolve(sketchDir, '..', 'temp', 'ai_temp_stats_cache');
  fs.mkdirSync(directory, {recursive: true});
  const binary = path.join(directory, 'sender-' + process.pid);
  const compiled = spawnSync('c++', ['-std=c++17', '-DSHOT_STOPPER_HOST_TEST',
    '-I' + sketchDir, '-x', 'c++', '-', '-o', binary], {input: native, encoding: 'utf8'});
  assert.equal(compiled.status, 0, compiled.stderr);
  const run = spawnSync(binary, [], {encoding: 'utf8'});
  assert.equal(run.status, 0, run.stderr);
  fs.unlinkSync(binary);
}
