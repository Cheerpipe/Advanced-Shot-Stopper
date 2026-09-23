#include "nimble_client_platform.h"
#include "EspressoScaleBLE.h"
#include "nimble/NimbleAdvertisement.h"
#include "nimble/NimbleResilience.h"
#include <cstdio>
#include <cassert>
#include <cstdarg>

static int unlockedSnprintf(char *out, size_t capacity, const char *format, ...) {
  assert(testCriticalDepth == 0);
  va_list args;
  va_start(args, format);
  const int result = vsnprintf(out, capacity, format, args);
  va_end(args);
  return result;
}

#define snprintf unlockedSnprintf
#include "../src/EspressoScaleBLENimble.cpp"
#undef snprintf
extern "C" void shotStopperScaleLog(uint8_t, const char *) {}

static unsigned checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)

namespace {
struct NimbleScaleClientTest {
static void ready(NimbleScaleClient &c) {
  testOnWait={}; testOnSubmit={}; testSubmitStatus=0;
  testTerminateStatus=0; testTerminations=0; testWrites=0; testWakeCount=0;
  testRuntimeReady=true; testSyncGeneration=1;
  c.beginGeneration(); c.lifecycleActive_=true; c.syncGeneration_=1;
  c.beginOperation(NimbleScaleClient::CallbackDomain::Link);
  c.connectionHandle_=1; c.readHandle_=10; c.writeHandle_=12;
  c.writeProperties_=BLE_GATT_CHR_PROP_WRITE;
  c.protocol_=&kScaleProtocolGenericFf11;
  c.finishReady();
}
static void complete(int status=0) {
  ble_gatt_error e={status,12};
  testWriteCallback(1,&e,nullptr,testWriteArg);
}
static void notify(NimbleScaleClient &c,uint16_t length) {
  os_mbuf b={}; b.length=length; b.data[0]=3;
  b.data[1]=0x0b;
  b.data[19]=0x08;
  c.onNotification(1,10,&b,c.linkOperationId_);
}
static void advertise(NimbleScaleClient &c, uint8_t eventType,
                      const char *name = "BOOKOO Mini") {
  testAdvertisementParseStatus = 0;
  testAdvertisementFields = {
      reinterpret_cast<uint8_t *>(const_cast<char *>(name)),
      static_cast<uint8_t>(strlen(name)), 1, 0, nullptr};
  ble_gap_disc_desc advertisement = {
      {0, {1, 2, 3, 4, 5, 6}}, eventType, nullptr, 0};
  c.onAdvertisement(advertisement, c.scanOperationId_);
}
static void run() {
  for (bool first : {false, true}) {
    for (bool stale : {false, true}) {
      for (bool wrap : {false, true}) {
        testNowMs=wrap ? UINT32_MAX-2000ULL : 100;
        NimbleScaleClient c(false); ready(c);
        if (!first) { notify(c,20); CHECK(c.newWeightAvailable()); }
        const uint32_t limit=first ? FIRST_PACKET_TIMEOUT_MS : 8000;
        testNowMs+=stale ? 0 : limit-1;
        notify(c,20);
        testNowMs+=stale ? limit : 1;
        CHECK(c.newWeightAvailable()==!stale);
        CHECK(c.isConnected()==!stale);
        if (stale) CHECK(c.lastReason()==(first
            ? ScaleDisconnectReason::FIRST_PACKET_TIMEOUT
            : ScaleDisconnectReason::PACKET_TIMEOUT));
        else CHECK(c.lastPacketAgeMs()==1);
      }
    }
  }
  for (bool first : {false, true}) {
    for (unsigned invalid=0; invalid<3; ++invalid) {
      testNowMs=100;
      NimbleScaleClient c(false); ready(c);
      if (!first) { notify(c,20); CHECK(c.newWeightAvailable()); }
      const uint32_t limit=first ? FIRST_PACKET_TIMEOUT_MS : 8000;
      testNowMs+=limit-1;
      if (invalid==0) notify(c,19); // Fresh but truncated.
      if (invalid==1) {
        os_mbuf b={}; b.length=20; b.data[0]=3; b.data[1]=0x0b; b.data[19]=8;
        c.onNotification(1,10,&b,c.linkOperationId_+1);
      }
      ++testNowMs;
      if (invalid==2) notify(c,20); // First valid frame arrived too late.
      CHECK(!c.newWeightAvailable()); CHECK(!c.isConnected());
      CHECK(c.lastReason()==(first ? ScaleDisconnectReason::FIRST_PACKET_TIMEOUT
                                  : ScaleDisconnectReason::PACKET_TIMEOUT));
    }
  }
  {
    NimbleScaleClient c(false); ready(c);
    os_mbuf b={}; b.length=20; b.data[0]=3; b.data[1]=0x0b;
    b.data[3]=0x12; b.data[4]=0x34;
    b.data[6]='-'; b.data[8]=0x1f; b.data[9]=0x40;
    for (unsigned i=0;i<19;++i) b.data[19]^=b.data[i];
    for (unsigned invalid=0;invalid<2;++invalid) {
      b.data[invalid ? 19 : 1]^=1;
      if (!invalid) b.data[19]^=1; // Wrong type with an otherwise valid checksum.
      c.onNotification(1,10,&b,c.linkOperationId_);
      CHECK(!c.newWeightAvailable());
      CHECK(!c.hasValidPacket_);
      b.data[invalid ? 19 : 1]^=1;
      if (!invalid) b.data[19]^=1;
    }
    c.onNotification(1,10,&b,c.linkOperationId_);
    CHECK(c.newWeightAvailable()); CHECK(c.weight()==-80.0f);
    CHECK(c.currentTimerMs_==0x1234);
    CHECK(c.rejectedPackets()==2);
    const uint8_t codes[]={1,4,5,6,7,2,0x15}; unsigned command=0;
    for (ScaleOp op : {ScaleOp::Tare,ScaleOp::StartTimer,ScaleOp::StopTimer,
                      ScaleOp::ResetTimer,ScaleOp::CombinedTareStart,ScaleOp::SetVolume,
                      ScaleOp::PowerOff}) {
      uint8_t payload[SCALE_MAX_COMMAND_LENGTH]={}; int length=0;
      CHECK(kScaleProtocolGenericFf11.encodeCommand(op,3,payload,&length));
      CHECK(length==6); uint8_t sum=0;
      CHECK(payload[0]==3 && payload[1]==0x0a && payload[2]==codes[command++]);
      CHECK(payload[3]==0 && payload[4]==(op==ScaleOp::SetVolume ? 3 : 0));
      for (int i=0;i<length-1;++i) sum^=payload[i];
      CHECK(payload[length-1]==sum);
    }
    CHECK(kScaleProtocolGenericFf11.features.has(ScaleFeaturePowerOff));
    CHECK(!kScaleProtocolAcaia.features.has(ScaleFeaturePowerOff));
    CHECK(!kScaleProtocolDifluid.features.has(ScaleFeaturePowerOff));
    CHECK(!kScaleProtocolFelicita.features.has(ScaleFeaturePowerOff));
  }
  {
    NimbleScaleClient c(false);
    c.beginGeneration();
    c.state_ = NimbleScaleClient::State::Scanning;
    const uint32_t operation = c.beginOperation(NimbleScaleClient::CallbackDomain::Scan);
    uint8_t firstName[] = "BOOKOO first";
    uint8_t secondName[] = "BOOKOO second";
    testAdvertisementParseStatus = 0;
    testAdvertisementFields = {firstName, sizeof(firstName) - 1, 1, 0, nullptr};
    ble_gap_disc_desc first = {{0, {1, 2, 3, 4, 5, 6}},
                              BLE_HCI_ADV_RPT_EVTYPE_ADV_IND, nullptr, 0};
    c.onAdvertisement(first, operation);
    char mac[32] = {};
    char name[32] = {};
    // Publication after the consumption lock releases must remain pending,
    // while the first call returns its coherent address/name pair.
    testAfterCriticalExit = [&] {
      testAdvertisementFields.name = secondName;
      testAdvertisementFields.name_len = sizeof(secondName) - 1;
      first.addr.val[0] = 7;
      c.onAdvertisement(first, operation);
    };
    CHECK(c.takeSeenAdvertisement(mac, sizeof(mac), name, sizeof(name)));
    CHECK(strcmp(mac, "06:05:04:03:02:01") == 0);
    CHECK(strcmp(name, "BOOKOO first") == 0);
    CHECK(c.takeSeenAdvertisement(mac, sizeof(mac), name, sizeof(name)));
    CHECK(strcmp(mac, "06:05:04:03:02:07") == 0);
    CHECK(strcmp(name, "BOOKOO second") == 0);
    CHECK(!c.takeSeenAdvertisement(mac, sizeof(mac), name, sizeof(name)));
    c.onAdvertisement(first, operation + 1);
    CHECK(!c.takeSeenAdvertisement(mac, sizeof(mac), name, sizeof(name)));
    // Latest identity survives candidate-slot eviction and small destinations
    // keep the public truncation/termination behavior.
    for (unsigned i = 0; i < kCandidateCount + 2; ++i) {
      first.addr.val[0] = static_cast<uint8_t>(i);
      c.onAdvertisement(first, operation);
    }
    CHECK(c.takeSeenAdvertisement(mac, 3, name, 1));
    CHECK(strcmp(mac, "06") == 0 && name[0] == '\0');
    c.onAdvertisement(first, operation);
    CHECK(c.takeSeenAdvertisement(nullptr, 0, nullptr, 0));
    CHECK(!c.seenPending_ && testCriticalDepth == 0);
    testAdvertisementParseStatus = BLE_HS_EINVAL;
    testAdvertisementFields = {};
  }
  {
    NimbleScaleClient c(false);
    CHECK(c.beginConfiguredScan(false));
    c.enterState(NimbleScaleClient::State::Backoff, 1);
    advertise(c, BLE_HCI_ADV_RPT_EVTYPE_ADV_IND);
    CHECK(!c.candidatePending_);
    c.enterState(NimbleScaleClient::State::Scanning);
    advertise(c, BLE_HCI_ADV_RPT_EVTYPE_ADV_NONCONN_IND);
    CHECK(!c.candidatePending_);

    c.clearScanData();
    advertise(c, BLE_HCI_ADV_RPT_EVTYPE_ADV_IND, "");
    CHECK(!c.candidatePending_);
    advertise(c, BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP);
    CHECK(c.candidatePending_);
  }
  {
    NimbleScaleClient c(false); ready(c);
    c.finishLink(false, ScaleDisconnectReason::REMOTE_DISCONNECTED,
                 BLE_HS_HCI_ERR(0x13));
    testNowMs = c.backoff_.deadlineMs();
    c.service();
    CHECK(c.state_ == NimbleScaleClient::State::Scanning);
    CHECK(c.connectAttemptsTotal_ == 0);
  }
  {
    NimbleScaleClient c(false);
    CHECK(c.beginConfiguredScan(false));
    for (unsigned attempt = 0; attempt < 12; ++attempt) {
      if (c.state_ == NimbleScaleClient::State::Backoff) {
        testNowMs = c.backoff_.deadlineMs();
        c.service();
      }
      advertise(c, BLE_HCI_ADV_RPT_EVTYPE_ADV_IND);
      c.service();
      testNowMs += SCALE_CONNECT_SETTLE_MS;
      c.service();
      CHECK(c.state_ == NimbleScaleClient::State::Backoff);
    }
    CHECK(c.connectAttemptsTotal_ == 12);
    CHECK(c.backoff_.failureCount() == 12);
    testNowMs = c.backoff_.deadlineMs();
    c.service();
    advertise(c, BLE_HCI_ADV_RPT_EVTYPE_ADV_IND);
    c.service();
    CHECK(c.isConnecting());
  }
  {
    NimbleScaleClient c(false); ready(c);
    testOnSubmit=[] { xTaskNotifyGive(xTaskGetCurrentTaskHandle()); };
    testOnWait=[] { complete(); };
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::Ok);
    CHECK(testTerminations==0);
    CHECK(testWakeCount==1); // ATT must not consume the worker's wakeup.
  }
  {
    NimbleScaleClient c(false); ready(c);
    testOnSubmit=[] { complete(); };
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::Ok);
  }
  {
    NimbleScaleClient c(false); ready(c);
    ScaleProtocol timerOnly=*c.protocol_;
    timerOnly.parseWeight=[](const uint8_t *,int,float *) { return false; };
    timerOnly.parseTimer=[](const uint8_t *,int,uint32_t *timer) {
      *timer=0;
      return true;
    };
    c.protocol_=&timerOnly;
    for (size_t i=0;i<kRxFrameCount;++i) notify(c,20);
    size_t replenished=0;
    std::function<void()> replenish;
    replenish=[&] {
      if (c.rxCount_<kRxFrameCount) {
        testAfterCriticalExit={};
        notify(c,20);
        ++replenished;
      }
      testAfterCriticalExit=replenish;
    };
    testAfterCriticalExit=replenish;
    CHECK(!c.newWeightAvailable());
    testAfterCriticalExit={};
    CHECK(replenished==kRxFrameCount);
    CHECK(c.rxCount_==kRxFrameCount);
  }
  for (const int status : {BLE_HS_EBUSY, BLE_HS_EAGAIN, BLE_HS_ENOMEM}) {
    NimbleScaleClient c(false); ready(c);
    testSubmitStatus=status;
    CHECK(c.writeOp(ScaleOp::SetVolume)==ScaleCommandResult::WriteFailed);
    CHECK(c.isConnected()); CHECK(testTerminations==0);
    CHECK(c.diagnostics().commandStatus==status);
    CHECK(c.diagnostics().disconnectSequence==0);
    testSubmitStatus=0; testOnSubmit=[] { complete(); };
    CHECK(c.writeOp(ScaleOp::StopTimer)==ScaleCommandResult::Ok);
    CHECK(c.diagnostics().commandStatus==status);
  }
  {
    NimbleScaleClient c(false); ready(c);
    testOnWait=[] { complete(BLE_HS_ATT_ERR(3)); };
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::WriteFailed);
    CHECK(c.isConnected()); CHECK(testTerminations==0);
    CHECK(c.diagnostics().commandStatus==BLE_HS_ATT_ERR(3));
  }
  for (const bool response : {false,true}) {
    NimbleScaleClient c(false); ready(c);
    if (!response) c.writeProperties_=BLE_GATT_CHR_PROP_WRITE_NO_RSP;
    testSubmitStatus=BLE_HS_EUNKNOWN;
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::WriteFailed);
    CHECK(!c.isConnected()); CHECK(testTerminations==1);
  }
  {
    NimbleScaleClient c(false); ready(c);
    c.writeProperties_=BLE_GATT_CHR_PROP_WRITE_NO_RSP;
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::Ok);
    testSubmitStatus=BLE_HS_ENOMEM;
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::WriteFailed);
    CHECK(c.isConnected()); CHECK(testTerminations==0);
  }
  for (const bool response : {false,true}) {
    NimbleScaleClient c(false); ready(c);
    ScaleProtocol paced=*c.protocol_;
    paced.features.minimumCommandIntervalMs=100;
    c.protocol_=&paced;
    if (!response) c.writeProperties_=BLE_GATT_CHR_PROP_WRITE_NO_RSP;
    else testOnSubmit=[] { complete(); };
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::Ok);
    const uint64_t firstAt=testNowMs;
    CHECK(c.writeOp(ScaleOp::StopTimer)==ScaleCommandResult::Ok);
    CHECK(testNowMs-firstAt==100); CHECK(testWrites==2);
  }
  {
    testNowMs=UINT32_MAX-50ULL;
    NimbleScaleClient c(false); ready(c);
    c.writeProperties_=BLE_GATT_CHR_PROP_WRITE_NO_RSP;
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::Ok);
    const uint64_t firstAt=testNowMs;
    CHECK(c.writeOp(ScaleOp::StopTimer)==ScaleCommandResult::Ok);
    CHECK(testNowMs-firstAt==100); CHECK(testWrites==2);
  }
  {
    NimbleScaleClient c(false); ready(c);
    c.pendingDisconnect_=true;
    c.pendingDisconnectStatus_=BLE_HS_HCI_ERR(8);
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::WriteFailed);
    CHECK(testWrites==0); CHECK(!c.isConnected());
    CHECK(c.lastReason()==ScaleDisconnectReason::SUPERVISION_TIMEOUT);
  }
  {
    NimbleScaleClient c(false); ready(c);
    c.writeProperties_=BLE_GATT_CHR_PROP_WRITE_NO_RSP;
    CHECK(c.writeOp(ScaleOp::PowerOff)==ScaleCommandResult::Ok);
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::NotConnected);
    CHECK(testWrites==1);
    ready(c);
    c.writeProperties_=BLE_GATT_CHR_PROP_WRITE_NO_RSP;
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::Ok);
  }
  {
    NimbleScaleClient c(false); ready(c);
    const uint64_t before=testNowMs;
    testOnWait=[&] { testNowMs+=3; xSemaphoreGive(c.writeSignal_); };
    testTerminateStatus=BLE_HS_ENOTCONN;
    CHECK(c.writeOp(ScaleOp::CombinedTareStart)==ScaleCommandResult::WriteFailed);
    CHECK(testNowMs-before>=BLE_OPERATION_TIMEOUT_MS);
    CHECK(testNowMs-before<=BLE_OPERATION_TIMEOUT_MS+3);
    CHECK(testTerminations==1); CHECK(testWrites==1);
    CHECK(c.diagnostics().disconnectStatus==BLE_HS_ETIMEOUT);
    CHECK(c.diagnostics().teardownStatus==BLE_HS_ENOTCONN);
    CHECK(c.diagnostics().disconnectCommand==static_cast<uint8_t>(ScaleOp::CombinedTareStart));
    const auto oldCallback=testWriteCallback; void *oldArg=testWriteArg;
    ready(c);
    testOnWait=[&] {
      ble_gatt_error e={0,12}; oldCallback(1,&e,nullptr,oldArg);
      CHECK(!c.writeCompleted_);
      complete();
    };
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::Ok);
    CHECK(c.diagnostics().disconnectStatus==BLE_HS_ETIMEOUT);
  }
  for (const bool completionFirst : {false,true}) {
    NimbleScaleClient c(false); ready(c);
    testOnWait=[&] {
      if (completionFirst) complete();
      ble_gap_event e={}; e.type=BLE_GAP_EVENT_DISCONNECT;
      e.disconnect.reason=BLE_HS_HCI_ERR(8); e.disconnect.conn.conn_handle=1;
      c.onGapEvent(&e,c.linkOperationId_);
    };
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::WriteFailed);
    CHECK(c.lastReason()==ScaleDisconnectReason::SUPERVISION_TIMEOUT);
    CHECK(c.diagnostics().disconnectStatus==BLE_HS_HCI_ERR(8));
    CHECK(c.diagnostics().disconnectOrigin==ScaleBleDisconnectOrigin::Gap);
    CHECK(c.diagnostics().disconnectSequence==1); CHECK(testTerminations==0);
    c.service(); CHECK(c.diagnostics().disconnectSequence==1);
  }
  {
    NimbleScaleClient c(false); ready(c);
    c.activeCommand_=static_cast<uint8_t>(ScaleOp::Tare);
    c.commandStartedAt_=nowMs();
    c.pushCriticalEvent(NimbleScaleClient::EventType::Disconnected,
                        BLE_HS_HCI_ERR(8),1,c.linkOperationId_);
    c.finishLink(true,ScaleDisconnectReason::COMMAND_WRITE_FAILED,BLE_HS_ETIMEOUT);
    CHECK(c.lastReason()==ScaleDisconnectReason::SUPERVISION_TIMEOUT);
    CHECK(c.diagnostics().disconnectStatus==BLE_HS_HCI_ERR(8));
    CHECK(testTerminations==0);
  }
  {
    NimbleScaleClient c(false); ready(c);
    testOnWait=[] { testRuntimeReady=false; ++testSyncGeneration; };
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::WriteFailed);
    CHECK(c.lastReason()==ScaleDisconnectReason::HOST_RESET);
    CHECK(c.diagnostics().disconnectSequence==1);
    CHECK(testTerminations==0);
  }
  {
    NimbleScaleClient c(false); ready(c);
    testNowMs=UINT32_MAX-25ULL;
    const uint64_t before=testNowMs;
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::WriteFailed);
    CHECK(testNowMs-before==BLE_OPERATION_TIMEOUT_MS);
  }
  {
    NimbleScaleClient c(false); ready(c);
    for (unsigned i=0;i<8;++i) {
      notify(c,21); notify(c,20); CHECK(c.newWeightAvailable());
    }
    CHECK(c.isConnected()); CHECK(testTerminations==0);
    for (unsigned i=0;i<7;++i) notify(c,21);
    CHECK(c.isConnected());
    notify(c,21); CHECK(!c.isConnected()); CHECK(testTerminations==1);
  }
  {
    NimbleScaleClient c(false); ready(c);
    const uint32_t capturedAt = testNowMs;
    notify(c,20);
    const uint32_t beforeWrite = c.notificationSequence();
    testNowMs += 1200;
    CHECK(c.newWeightAvailable());
    CHECK(c.weightSample().receivedAtMs == capturedAt);
    CHECK(c.weightSample().captureSequence == beforeWrite);
    CHECK(c.weightSample().weightG == 0.0f);
    testOnWait=[&] { notify(c,20); complete(); };
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::Ok);
    CHECK(c.newWeightAvailable());
    CHECK(c.weightSample().captureSequence == beforeWrite + 1);
    CHECK(c.weightSample().receivedAtMs >= capturedAt + 1200);
    c.notificationSequence_=UINT32_MAX;
    notify(c,20);
    CHECK(c.newWeightAvailable());
    CHECK(c.weightSample().captureSequence == 1);
    c.writeProperties_=BLE_GATT_CHR_PROP_WRITE_NO_RSP;
    testOnWait={};
    CHECK(c.writeOp(ScaleOp::Tare)==ScaleCommandResult::Ok);
    CHECK(!c.newWeightAvailable()); // Transport success creates no weight evidence.
  }
  printf("NimBLE production client: %u checks passed\n",checks);
}
};
}
int main() { NimbleScaleClientTest::run(); }
