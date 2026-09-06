#pragma once

#include "shot_stopper_host_stubs.h"
#include "persistence_host_stubs.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

using esp_err_t = int;
using esp_ota_handle_t = uint32_t;

constexpr esp_err_t ESP_FAIL = -1;
constexpr int WIFI_PS_NONE = 0;

enum esp_ota_img_states_t {
  ESP_OTA_IMG_UNDEFINED = 0,
  ESP_OTA_IMG_PENDING_VERIFY = 1,
};

struct esp_partition_t {
  uint32_t size = 4U * 1024U * 1024U;
};

inline esp_partition_t otaHostRunningPartition;
inline esp_partition_t otaHostTargetPartition;

inline const esp_partition_t *esp_ota_get_running_partition() {
  return &otaHostRunningPartition;
}

inline const esp_partition_t *esp_ota_get_next_update_partition(const void *) {
  return &otaHostTargetPartition;
}

inline esp_err_t esp_ota_get_state_partition(const esp_partition_t *,
                                             esp_ota_img_states_t *state) {
  if (state == nullptr) return ESP_FAIL;
  *state = ESP_OTA_IMG_UNDEFINED;
  return ESP_OK;
}

inline esp_err_t esp_partition_read(const esp_partition_t *, size_t, void *out,
                                    size_t length) {
  if (out == nullptr) return ESP_FAIL;
  std::memset(out, 0, length);
  return ESP_OK;
}

inline esp_err_t esp_ota_resume(const esp_partition_t *, size_t, size_t,
                                esp_ota_handle_t *handle) {
  if (handle == nullptr) return ESP_FAIL;
  *handle = 1;
  return ESP_OK;
}

inline esp_err_t esp_ota_begin(const esp_partition_t *, size_t,
                               esp_ota_handle_t *handle) {
  if (handle == nullptr) return ESP_FAIL;
  *handle = 1;
  return ESP_OK;
}

inline esp_err_t esp_ota_write(esp_ota_handle_t, const void *, size_t) {
  return ESP_OK;
}
inline esp_err_t esp_ota_end(esp_ota_handle_t) { return ESP_OK; }
inline esp_err_t esp_ota_abort(esp_ota_handle_t) { return ESP_OK; }
inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t *) {
  return ESP_OK;
}
inline esp_err_t esp_ota_mark_app_valid_cancel_rollback() { return ESP_OK; }
inline bool esp_ota_check_rollback_is_possible() { return true; }
inline esp_err_t esp_ota_mark_app_invalid_rollback() { return ESP_OK; }
inline esp_err_t esp_wifi_set_ps(int) { return ESP_OK; }

inline void mbedtls_sha256_clone(mbedtls_sha256_context *destination,
                                 const mbedtls_sha256_context *source) {
  *destination = *source;
}
