#include "LineaMicraProtocol.h"

#include <cJSON.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace lineamicra {
namespace {

constexpr unsigned kMaxJsonDepth = 6;

unsigned jsonDepth(const cJSON *item) {
  unsigned childDepth = 0;
  for (const cJSON *child = item == nullptr ? nullptr : item->child; child != nullptr;
       child = child->next) {
    const unsigned depth = jsonDepth(child);
    if (depth > childDepth) childDepth = depth;
  }
  return item == nullptr ? 0U : 1U + childDepth;
}

cJSON *parseRoot(const char *json, size_t length, Error &error) {
  error = Error::NONE;
  if (json == nullptr || length == 0) {
    error = Error::INVALID_ARGUMENT;
    return nullptr;
  }
  if (length > kMaxResponseBytes) {
    error = Error::OVERSIZED;
    return nullptr;
  }
  char bounded[kMaxResponseBytes + 1];
  std::memcpy(bounded, json, length);
  bounded[length] = '\0';
  const char *end = nullptr;
  cJSON *root = cJSON_ParseWithOpts(bounded, &end, true);
  if (root == nullptr) {
    error = Error::MALFORMED;
    return nullptr;
  }
  if (jsonDepth(root) > kMaxJsonDepth) {
    cJSON_Delete(root);
    error = Error::TOO_DEEP;
    return nullptr;
  }
  return root;
}

const cJSON *uniqueField(const cJSON *object, const char *name, bool required,
                         Error &error) {
  const cJSON *match = nullptr;
  for (const cJSON *child = object == nullptr ? nullptr : object->child;
       child != nullptr; child = child->next) {
    if (child->string != nullptr && std::strcmp(child->string, name) == 0) {
      if (match != nullptr) {
        error = Error::DUPLICATE_FIELD;
        return nullptr;
      }
      match = child;
    }
  }
  if (required && match == nullptr) error = Error::MISSING_FIELD;
  return match;
}

bool copyString(const cJSON *item, char *output, size_t capacity, Error &error) {
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    error = Error::WRONG_TYPE;
    return false;
  }
  const size_t length = std::strlen(item->valuestring);
  if (length >= capacity) {
    error = Error::INVALID_VALUE;
    return false;
  }
  std::memcpy(output, item->valuestring, length + 1);
  return true;
}

bool numberToDeciC(const cJSON *item, bool target, uint16_t &output, Error &error) {
  if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
      item->valuedouble < 0.0) {
    error = Error::INVALID_VALUE;
    return false;
  }
  if (target && (item->valuedouble < 80.0 || item->valuedouble > 100.0)) {
    error = Error::OUT_OF_RANGE;
    return false;
  }
  const double deci = std::round(item->valuedouble * 10.0);
  if (deci < 0.0 || deci > 65535.0) {
    error = Error::OUT_OF_RANGE;
    return false;
  }
  output = static_cast<uint16_t>(deci);
  return true;
}

}  // namespace

bool validToken(const char *token, size_t length) {
  if (token == nullptr || length != kTokenLength) return false;
  for (size_t index = 0; index < length; ++index) {
    const unsigned char byte = static_cast<unsigned char>(token[index]);
    if (byte < 0x20U || byte > 0x7eU) return false;
  }
  return true;
}

bool buildQuery(Query query, char *output, size_t capacity, size_t &payloadLength) {
  const char *text = nullptr;
  switch (query) {
    case Query::MACHINE_CAPABILITIES: text = "machineCapabilities"; break;
    case Query::BOILERS: text = "boilers"; break;
    case Query::MACHINE_MODE: text = "machineMode"; break;
  }
  const size_t length = std::strlen(text) + 1;
  if (output == nullptr || capacity < length) return false;
  std::memcpy(output, text, length);
  payloadLength = length;
  return true;
}

bool buildSetBrewTarget(uint16_t targetDeciC, char *output, size_t capacity,
                        size_t &payloadLength) {
  if (output == nullptr || targetDeciC < 800 || targetDeciC > 1000) return false;
  const int written = std::snprintf(
      output, capacity,
      "{\"name\":\"SettingBoilerTarget\",\"parameter\":{\"identifier\":\"CoffeeBoiler1\",\"value\":%u.%u}}",
      targetDeciC / 10U, targetDeciC % 10U);
  if (written < 0 || static_cast<size_t>(written) >= capacity) return false;
  payloadLength = static_cast<size_t>(written) + 1;
  return true;
}

bool parseMachineCapabilities(const char *json, size_t length, Error &error) {
  cJSON *root = parseRoot(json, length, error);
  if (root == nullptr) return false;
  bool valid = false;
  if (!cJSON_IsArray(root) || cJSON_GetArraySize(root) != 1) {
    error = Error::WRONG_TYPE;
  } else {
    const cJSON *entry = cJSON_GetArrayItem(root, 0);
    const cJSON *family = uniqueField(entry, "family", true, error);
    if (error == Error::NONE && !cJSON_IsString(family)) {
      error = Error::WRONG_TYPE;
    } else if (error == Error::NONE &&
               std::strcmp(family->valuestring, "MICRA") != 0 &&
               std::strcmp(family->valuestring, "LINEAMICRA") != 0) {
      error = Error::UNSUPPORTED_MODEL;
    } else {
      valid = error == Error::NONE;
    }
  }
  cJSON_Delete(root);
  return valid;
}

bool parseBrewBoiler(const char *json, size_t length, BrewBoiler &boiler,
                     Error &error) {
  cJSON *root = parseRoot(json, length, error);
  if (root == nullptr) return false;
  BrewBoiler parsed;
  unsigned matches = 0;
  if (!cJSON_IsArray(root)) {
    error = Error::WRONG_TYPE;
  } else {
    for (const cJSON *entry = root->child; entry != nullptr && error == Error::NONE;
         entry = entry->next) {
      if (!cJSON_IsObject(entry)) {
        error = Error::WRONG_TYPE;
        break;
      }
      const cJSON *id = uniqueField(entry, "id", true, error);
      if (error != Error::NONE || !cJSON_IsString(id)) {
        if (error == Error::NONE) error = Error::WRONG_TYPE;
        break;
      }
      if (std::strcmp(id->valuestring, "CoffeeBoiler1") != 0) continue;
      if (++matches > 1) {
        error = Error::DUPLICATE_BOILER;
        break;
      }
      const cJSON *current = uniqueField(entry, "current", true, error);
      const cJSON *target = uniqueField(entry, "target", true, error);
      const cJSON *enabled = uniqueField(entry, "isEnabled", false, error);
      if (error != Error::NONE ||
          !numberToDeciC(current, false, parsed.currentDeciC, error) ||
          !numberToDeciC(target, true, parsed.targetDeciC, error)) {
        break;
      }
      if (enabled != nullptr) {
        if (!cJSON_IsBool(enabled)) {
          error = Error::WRONG_TYPE;
          break;
        }
        parsed.enabled = cJSON_IsTrue(enabled);
      }
    }
  }
  if (error == Error::NONE && matches == 0) error = Error::MISSING_FIELD;
  cJSON_Delete(root);
  if (error != Error::NONE) return false;
  boiler = parsed;
  return true;
}

bool parseMachineMode(const char *json, size_t length, Mode &mode, Error &error) {
  cJSON *root = parseRoot(json, length, error);
  if (root == nullptr) return false;
  bool valid = cJSON_IsString(root) && root->valuestring != nullptr;
  if (!valid) {
    error = Error::WRONG_TYPE;
  } else if (std::strcmp(root->valuestring, "StandBy") == 0) {
    mode = Mode::STANDBY;
  } else if (std::strcmp(root->valuestring, "BrewingMode") == 0) {
    mode = Mode::BREWING;
  } else if (std::strcmp(root->valuestring, "EcoMode") == 0) {
    mode = Mode::ECO;
  } else {
    mode = Mode::UNSUPPORTED;
  }
  cJSON_Delete(root);
  return valid;
}

bool parseCommandStatus(const char *json, size_t length, CommandStatus &status,
                        Error &error) {
  cJSON *root = parseRoot(json, length, error);
  if (root == nullptr) return false;
  CommandStatus parsed;
  if (!cJSON_IsObject(root)) {
    error = Error::WRONG_TYPE;
  } else {
    const cJSON *id = uniqueField(root, "id", true, error);
    const cJSON *message = uniqueField(root, "message", true, error);
    const cJSON *value = uniqueField(root, "status", true, error);
    if (error == Error::NONE && copyString(id, parsed.id, sizeof(parsed.id), error) &&
        copyString(message, parsed.message, sizeof(parsed.message), error) &&
        copyString(value, parsed.status, sizeof(parsed.status), error)) {
      parsed.success = std::strcmp(parsed.status, "success") == 0;
    }
  }
  cJSON_Delete(root);
  if (error != Error::NONE) return false;
  status = parsed;
  return true;
}

}  // namespace lineamicra
