#include "ShotStopperMicraObservation.h"

#include <cJSON.h>
#include <cmath>
#include <cstring>

namespace shotstopper {
namespace {
const char *text(const cJSON *object, const char *key) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
  for (const cJSON *field = value ? value->next : nullptr; field; field = field->next)
    if (field->string && strcmp(field->string, key) == 0) return nullptr;
  return cJSON_IsString(value) ? value->valuestring : nullptr;
}

LineaMicraObservedMode mode(const char *value) {
  if (strcmp(value, "StandBy") == 0) return LineaMicraObservedMode::STANDBY;
  if (strcmp(value, "BrewingMode") == 0) return LineaMicraObservedMode::BREWING;
  if (strcmp(value, "EcoMode") == 0) return LineaMicraObservedMode::ECO;
  return LineaMicraObservedMode::UNSUPPORTED;
}
}  // namespace

bool decodeMicraDashboard(const cJSON *root, MicraObservation &update) {
  if (!cJSON_IsObject(root)) return false;
  unsigned seen = 0;
  for (const cJSON *field = root->child; field; field = field->next) {
    if (!field->string) return false;
    const unsigned bit = strcmp(field->string, "connected") == 0 ? 1U
        : strcmp(field->string, "widgets") == 0 ? 2U
        : strcmp(field->string, "removedWidgets") == 0 ? 4U : 0U;
    if (seen & bit) return false;
    seen |= bit;
  }
  MicraObservation next = update;
  const cJSON *connected = cJSON_GetObjectItemCaseSensitive(root, "connected");
  if (connected != nullptr) {
    if (!cJSON_IsBool(connected)) return false;
    next.connectedPresent = true;
    next.connected = cJSON_IsTrue(connected);
  }
  const cJSON *widgets = cJSON_GetObjectItemCaseSensitive(root, "widgets");
  if (widgets != nullptr && !cJSON_IsArray(widgets)) return false;
  const cJSON *widget = nullptr;
  cJSON_ArrayForEach(widget, widgets) {
    if (!cJSON_IsObject(widget)) return false;
    const char *code = text(widget, "code");
    if (code == nullptr) return false;
    const bool power = strcmp(code, "CMMachineStatus") == 0;
    const bool boiler = strcmp(code, "CMCoffeeBoiler") == 0;
    const bool cleaning = strcmp(code, "CMBackFlush") == 0;
    if (!power && !boiler && !cleaning) continue;
    const cJSON *output = cJSON_GetObjectItemCaseSensitive(widget, "output");
    if (!cJSON_IsObject(output)) return false;
    for (const cJSON *field = output->next; field; field = field->next)
      if (field->string && strcmp(field->string, "output") == 0) return false;
    const char *key = power ? "mode" : boiler ? "targetTemperature" : "status";
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(output, key);
    for (const cJSON *field = value ? value->next : nullptr; field; field = field->next)
      if (field->string && strcmp(field->string, key) == 0) return false;
    if (power) {
      if (value == nullptr) continue;
      if (!cJSON_IsString(value) || value->valuestring == nullptr) return false;
      if (next.powerPresent) return false;
      next.powerPresent = true;
      next.mode = mode(value->valuestring);
    } else if (boiler) {
      if (value == nullptr) continue;
      if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
          value->valuedouble < LINEA_MICRA_BREW_TARGET_MIN_DECI_C / 10.0 ||
          value->valuedouble > LINEA_MICRA_BREW_TARGET_MAX_DECI_C / 10.0 ||
          next.temperaturePresent) return false;
      next.temperaturePresent = next.temperatureValid = true;
      next.targetDeciC = static_cast<uint16_t>(std::lround(value->valuedouble * 10.0));
    } else if (next.source == MicraObservationSource::WEBSOCKET) {
      if (value == nullptr) continue;
      if (!cJSON_IsString(value) || value->valuestring == nullptr ||
          next.cleaningPresent) return false;
      next.cleaningPresent = next.cleaningAvailable = true;
      const char *label = value->valuestring;
      next.cleaning = strcmp(label, "Off") == 0 ? MicraCleaningState::OFF
          : strcmp(label, "Requested") == 0 ? MicraCleaningState::REQUESTED
          : strcmp(label, "Cleaning") == 0 ? MicraCleaningState::CLEANING
          : MicraCleaningState::UNKNOWN;
      size_t length = std::strlen(label);
      if (length >= sizeof(next.cleaningLabel)) {
        length = sizeof(next.cleaningLabel) - 1;
        while (length && (static_cast<unsigned char>(label[length]) & 0xc0) == 0x80) --length;
      }
      std::memcpy(next.cleaningLabel, label, length);
      next.cleaningLabel[length] = '\0';
    }
  }
  const cJSON *removed = cJSON_GetObjectItemCaseSensitive(root, "removedWidgets");
  if (removed != nullptr && !cJSON_IsArray(removed)) return false;
  cJSON_ArrayForEach(widget, removed) {
    const char *code = cJSON_IsString(widget) ? widget->valuestring : text(widget, "code");
    if (code == nullptr) return false;
    if (strcmp(code, "CMMachineStatus") == 0) {
      next.powerPresent = true;
      next.mode = LineaMicraObservedMode::NONE;
    } else if (strcmp(code, "CMCoffeeBoiler") == 0) {
      next.temperaturePresent = true;
      next.temperatureValid = false;
      next.targetDeciC = 0;
    } else if (strcmp(code, "CMBackFlush") == 0 &&
               next.source == MicraObservationSource::WEBSOCKET) {
      if (next.cleaningPresent) return false;
      next.cleaningPresent = true;
      next.cleaningAvailable = false;
      next.cleaning = MicraCleaningState::UNKNOWN;
      next.cleaningLabel[0] = '\0';
    }
  }
  update = next;
  return widgets != nullptr || removed != nullptr || connected != nullptr;
}
}  // namespace shotstopper
