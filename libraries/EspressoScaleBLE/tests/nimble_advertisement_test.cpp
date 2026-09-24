#include "nimble/NimbleAdvertisement.h"
#include "ScaleProtocol.h"

#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

int checks = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ << ": " \
                << #condition << std::endl;                                    \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

void testAdvAndScanResponseMerge() {
  const uint8_t advertisement[] = {3, 0x03, 0x11, 0xff};
  const uint8_t scanResponse[] = {
      7, 0x09, 'B', 'O', 'O', 'K', 'O', 'O'};
  NimbleAdvertisementData data = {};
  CHECK(nimbleAccumulateAdvertisement(advertisement,
                                      sizeof(advertisement), data));
  CHECK(data.compatibleUuid16);
  CHECK(!data.namePresent);
  CHECK(nimbleAccumulateAdvertisement(scanResponse, sizeof(scanResponse),
                                      data));
  CHECK(data.nameComplete);
  CHECK(std::strcmp(data.name, "BOOKOO") == 0);
  CHECK(nimbleAdvertisementIsCompatible(data));
}

void testMalformedAndBounds() {
  NimbleAdvertisementData data = {};
  const uint8_t malformed[] = {5, 0x09, 'A'};
  CHECK(!nimbleAccumulateAdvertisement(malformed, sizeof(malformed), data));
  CHECK(!data.namePresent);
  CHECK(!nimbleAccumulateAdvertisement(nullptr, 1, data));

  uint8_t longName[40] = {};
  longName[0] = 39;
  longName[1] = 0x09;
  for (size_t i = 2; i < sizeof(longName); ++i) {
    longName[i] = 'X';
  }
  CHECK(nimbleAccumulateAdvertisement(longName, sizeof(longName), data));
  CHECK(data.name[NIMBLE_SCALE_ADV_NAME_CAPACITY - 1] == '\0');
}

void testShortNameCannotReplaceCompleteName() {
  const uint8_t complete[] = {7, 0x09, 'B', 'O', 'O', 'K', 'O', 'O'};
  const uint8_t shortened[] = {4, 0x08, 'A', 'C', 'A'};
  NimbleAdvertisementData data = {};
  CHECK(nimbleAccumulateAdvertisement(complete, sizeof(complete), data));
  CHECK(nimbleAccumulateAdvertisement(shortened, sizeof(shortened), data));
  CHECK(std::strcmp(data.name, "BOOKOO") == 0);
}

void fuzzEveryTruncation() {
  const uint8_t payload[] = {
      2, 0x01, 0x06, 3, 0x03, 0x11, 0xff,
      7, 0x09, 'B', 'O', 'O', 'K', 'O', 'O'};
  for (size_t length = 0; length <= sizeof(payload); ++length) {
    NimbleAdvertisementData data = {};
    const bool valid =
        nimbleAccumulateAdvertisement(payload, length, data);
    CHECK(valid == (length == 0 || length == 3 || length == 7 ||
                    length == sizeof(payload)));
  }
}

void testProtocolInitializationContracts() {
  const uint8_t prohibitedSmoothing[] = {0x03, 0x0a, 0x08,
                                         0x00, 0x00, 0x01};
  bool sawBookoo = false;
  bool sawAcaiaInitialization = false;
  bool sawFelicitaInitialization = false;
  bool sawDifluidInitialization = false;
  for (size_t index = 0; index < scaleProtocolCount(); ++index) {
    const ScaleProtocol *protocol = scaleProtocolAt(index);
    CHECK(protocol != nullptr);
    if (std::strcmp(protocol->id, "bookoo_generic") == 0) {
      sawBookoo = true;
      CHECK(protocol->initWrites == nullptr);
      CHECK(protocol->initWriteCount == 0);
    } else if (std::strcmp(protocol->id, "acaia") == 0) {
      sawAcaiaInitialization = protocol->initWriteCount > 0;
    } else if (std::strcmp(protocol->id, "felicita") == 0) {
      sawFelicitaInitialization = protocol->initWriteCount > 0;
    } else if (std::strcmp(protocol->id, "difluid") == 0) {
      sawDifluidInitialization = protocol->initWriteCount > 0;
    }
    for (size_t write = 0; write < protocol->initWriteCount; ++write) {
      const ScalePayload &payload = protocol->initWrites[write];
      CHECK(payload.length != static_cast<int>(sizeof(prohibitedSmoothing)) ||
            std::memcmp(payload.data, prohibitedSmoothing,
                        sizeof(prohibitedSmoothing)) != 0);
    }
    for (uint8_t rawOp = static_cast<uint8_t>(ScaleOp::Tare);
         rawOp <= static_cast<uint8_t>(ScaleOp::SetVolume); ++rawOp) {
      uint8_t command[SCALE_MAX_COMMAND_LENGTH] = {};
      int length = 0;
      if (protocol->encodeCommand(
              static_cast<ScaleOp>(rawOp), 0, command, &length)) {
        CHECK(length != static_cast<int>(sizeof(prohibitedSmoothing)) ||
              std::memcmp(command, prohibitedSmoothing,
                          sizeof(prohibitedSmoothing)) != 0);
      }
    }
  }
  CHECK(sawBookoo);
  CHECK(sawAcaiaInitialization);
  CHECK(sawFelicitaInitialization);
  CHECK(sawDifluidInitialization);
}

void testEurekaWeightFrame() {
  uint8_t frame[11] = {0xaa, 0x09, 0x41, 0, 0, 0, 0, 0x68, 0x01, 0, 0};
  float weight = 0;
  CHECK(kScaleProtocolEureka.parseWeight(frame, sizeof(frame), &weight));
  CHECK(weight == 36.0f);
  frame[6] = 1;
  CHECK(kScaleProtocolEureka.parseWeight(frame, sizeof(frame), &weight));
  CHECK(weight == -36.0f);
  frame[6] = 2;
  CHECK(!kScaleProtocolEureka.parseWeight(frame, sizeof(frame), &weight));
  frame[6] = 0;
  for (int index = 0; index < 3; ++index) {
    const uint8_t original = frame[index];
    frame[index] = 0;
    CHECK(!kScaleProtocolEureka.parseWeight(frame, sizeof(frame), &weight));
    frame[index] = original;
  }
  CHECK(!kScaleProtocolEureka.parseWeight(frame, sizeof(frame) - 1, &weight));
}

void fuzzProtocolPacketBounds() {
  for (size_t index = 0; index < scaleProtocolCount(); ++index) {
    const ScaleProtocol *protocol = scaleProtocolAt(index);
    for (int length = 0; length <= SCALE_MAX_PACKET_LENGTH; ++length) {
      std::vector<uint8_t> frame(static_cast<size_t>(length));
      for (uint8_t seed = 0; seed < 16; ++seed) {
        for (int offset = 0; offset < length; ++offset) {
          frame[static_cast<size_t>(offset)] =
              static_cast<uint8_t>(seed * 31U + offset * 17U);
        }
        float weight = 0;
        uint32_t timerMs = 0;
        if (protocol->parseWeight != nullptr &&
            protocol->parseWeight(frame.data(), length, &weight)) {
          CHECK(std::isfinite(weight));
          CHECK(std::fabs(weight) <= SCALE_MAX_WEIGHT_GRAMS);
        }
        if (protocol->parseTimer != nullptr) {
          protocol->parseTimer(frame.data(), length, &timerMs);
        }
      }
    }
  }
}

}  // namespace

int main() {
  testAdvAndScanResponseMerge();
  testMalformedAndBounds();
  testShortNameCannotReplaceCompleteName();
  fuzzEveryTruncation();
  testProtocolInitializationContracts();
  testEurekaWeightFrame();
  fuzzProtocolPacketBounds();
  std::cout << "NimBLE advertisement tests passed: " << checks << " checks\n";
  return 0;
}
