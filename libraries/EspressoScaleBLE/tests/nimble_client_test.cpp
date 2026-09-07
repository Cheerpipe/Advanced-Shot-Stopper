#include "nimble_client_platform.h"
#include "EspressoScaleBLE.h"
#include "nimble/NimbleAdvertisement.h"
#include "nimble/NimbleResilience.h"
#include <cstdio>
#include <cassert>

#include "../src/EspressoScaleBLENimble.cpp"
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
  c.onNotification(1,10,&b,c.linkOperationId_);
}
static void run() {
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
  printf("NimBLE production client: %u checks passed\n",checks);
}
};
}
int main() { NimbleScaleClientTest::run(); }
