#include "LineaMicraProtocol.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <string>

using lineamicra::Error;

static void testFraming() {
  char output[160] = {};
  size_t length = 0;
  assert(lineamicra::buildQuery(lineamicra::Query::BOILERS, output,
                               sizeof(output), length));
  assert(length == 8 && std::memcmp(output, "boilers\0", 8) == 0);
  assert(lineamicra::buildQuery(lineamicra::Query::MACHINE_MODE, output,
                               sizeof(output), length));
  assert(length == 12 && std::memcmp(output, "machineMode\0", 12) == 0);
  assert(lineamicra::buildQuery(lineamicra::Query::MACHINE_CAPABILITIES, output,
                               sizeof(output), length));
  assert(std::memcmp(output, "machineCapabilities\0", length) == 0);
  assert(!lineamicra::buildQuery(lineamicra::Query::BOILERS, output, 7, length));
  assert(lineamicra::buildSetBrewTarget(935, output, sizeof(output), length));
  assert(std::strcmp(output,
      "{\"name\":\"SettingBoilerTarget\",\"parameter\":{\"identifier\":\"CoffeeBoiler1\",\"value\":93.5}}") == 0);
  assert(length == std::strlen(output) + 1);
  assert(lineamicra::buildSetBrewTarget(800, output, sizeof(output), length));
  assert(std::strstr(output, "\"value\":80.0") != nullptr);
  assert(lineamicra::buildSetBrewTarget(1000, output, sizeof(output), length));
  assert(std::strstr(output, "\"value\":100.0") != nullptr);
  assert(!lineamicra::buildSetBrewTarget(799, output, sizeof(output), length));
  assert(!lineamicra::buildSetBrewTarget(1001, output, sizeof(output), length));
}

static void testToken() {
  std::string token(64, 'A');
  assert(lineamicra::validToken(token.data(), token.size()));
  assert(!lineamicra::validToken(token.data(), token.size() - 1));
  token[5] = '\0';
  assert(!lineamicra::validToken(token.data(), token.size()));
  token[5] = '\n';
  assert(!lineamicra::validToken(token.data(), token.size()));
}

static void testBoilers() {
  const char *json =
      "[{\"id\":\"SteamBoiler\",\"target\":131,\"current\":45},"
      "{\"id\":\"CoffeeBoiler1\",\"isEnabled\":true,\"target\":93.5,\"current\":25}]";
  lineamicra::BrewBoiler boiler;
  Error error = Error::NONE;
  assert(lineamicra::parseBrewBoiler(json, std::strlen(json), boiler, error));
  assert(boiler.currentDeciC == 250 && boiler.targetDeciC == 935 && boiler.enabled);

  const char *integer = "[{\"id\":\"CoffeeBoiler1\",\"target\":93,\"current\":65}]";
  assert(lineamicra::parseBrewBoiler(integer, std::strlen(integer), boiler, error));
  assert(boiler.targetDeciC == 930);

  const char *duplicate =
      "[{\"id\":\"CoffeeBoiler1\",\"target\":93,\"current\":65},"
      "{\"id\":\"CoffeeBoiler1\",\"target\":94,\"current\":66}]";
  assert(!lineamicra::parseBrewBoiler(duplicate, std::strlen(duplicate), boiler, error));
  assert(error == Error::DUPLICATE_BOILER);

  const char *duplicateField =
      "[{\"id\":\"CoffeeBoiler1\",\"target\":93,\"target\":94,\"current\":65}]";
  assert(!lineamicra::parseBrewBoiler(duplicateField, std::strlen(duplicateField), boiler, error));
  assert(error == Error::DUPLICATE_FIELD);

  for (const char *invalid : {
           "[]",
           "[{\"id\":\"CoffeeBoiler1\",\"target\":79.9,\"current\":25}]",
           "[{\"id\":\"CoffeeBoiler1\",\"target\":100.1,\"current\":25}]",
           "[{\"id\":\"CoffeeBoiler1\",\"target\":93,\"current\":-1}]",
           "[{\"id\":\"CoffeeBoiler1\",\"target\":\"93\",\"current\":25}]",
           "[{\"id\":\"CoffeeBoiler1\",\"target\":93}]"}) {
    assert(!lineamicra::parseBrewBoiler(invalid, std::strlen(invalid), boiler, error));
  }
  std::string oversized(513, ' ');
  assert(!lineamicra::parseBrewBoiler(oversized.data(), oversized.size(), boiler, error));
  assert(error == Error::OVERSIZED);
  const char *malformed = "[{\"id\":\"CoffeeBoiler1\"";
  assert(!lineamicra::parseBrewBoiler(malformed, std::strlen(malformed), boiler, error));
  assert(error == Error::MALFORMED);
  const char *nonfinite =
      "[{\"id\":\"CoffeeBoiler1\",\"target\":1e999,\"current\":25}]";
  assert(!lineamicra::parseBrewBoiler(nonfinite, std::strlen(nonfinite), boiler, error));
  const char *deep =
      "[{\"id\":\"CoffeeBoiler1\",\"target\":93,\"current\":25,\"x\":{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":1}}}}}}]";
  assert(!lineamicra::parseBrewBoiler(deep, std::strlen(deep), boiler, error));
  assert(error == Error::TOO_DEEP);
}

static void testModeCapabilitiesAndStatus() {
  Error error = Error::NONE;
  lineamicra::Mode mode = lineamicra::Mode::UNSUPPORTED;
  for (const auto &sample : {
           std::pair<const char *, lineamicra::Mode>{"\"StandBy\"", lineamicra::Mode::STANDBY},
           {"\"BrewingMode\"", lineamicra::Mode::BREWING},
           {"\"EcoMode\"", lineamicra::Mode::ECO},
           {"\"FutureMode\"", lineamicra::Mode::UNSUPPORTED}}) {
    assert(lineamicra::parseMachineMode(sample.first, std::strlen(sample.first), mode, error));
    assert(mode == sample.second);
  }
  assert(!lineamicra::parseMachineMode("true", 4, mode, error));
  const char *capabilities = "[{\"family\":\"MICRA\",\"machineModes\":[\"BrewingMode\",\"StandBy\"]}]";
  assert(lineamicra::parseMachineCapabilities(capabilities,
                                              std::strlen(capabilities), error));
  const char *wrong = "[{\"family\":\"GS3\"}]";
  assert(!lineamicra::parseMachineCapabilities(wrong, std::strlen(wrong), error));
  assert(error == Error::UNSUPPORTED_MODEL);

  const char *statusJson =
      "{\"id\":\"test-id\",\"message\":\"Success\",\"status\":\"success\"}";
  lineamicra::CommandStatus status;
  assert(lineamicra::parseCommandStatus(statusJson, std::strlen(statusJson), status, error));
  assert(status.success && std::strcmp(status.id, "test-id") == 0);
  const char *duplicateStatus =
      "{\"id\":\"a\",\"message\":\"x\",\"status\":\"success\",\"status\":\"error\"}";
  assert(!lineamicra::parseCommandStatus(duplicateStatus,
                                         std::strlen(duplicateStatus), status, error));
  assert(error == Error::DUPLICATE_FIELD);
}

int main() {
  testFraming();
  testToken();
  testBoilers();
  testModeCapabilitiesAndStatus();
  return 0;
}
