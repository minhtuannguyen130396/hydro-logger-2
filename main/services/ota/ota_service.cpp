#include "services/ota/ota_service.hpp"
#include "services/net/server_api.hpp"
#include "common/config.hpp"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "cJSON.h"

#include <cstring>

static const char* TAG = "OTA";

bool OtaService::checkAndUpdate(const std::string& versionUrl, const std::string& binUrl, LogBuffer& log) {
  // Step 1: Fetch remote version
  std::string remoteVersion;
  if (!fetchAndParseVersion(versionUrl, remoteVersion, log)) {
    log.appendf("[OTA] version fetch/parse failed\n");
    return false;
  }

  // Step 2: Compare with current version
  if (!needsUpdate(remoteVersion)) {
    log.appendf("[OTA] fw up-to-date (%s)\n", cfg::kCurrentFwVersion);
    ESP_LOGI(TAG, "firmware up-to-date: current=%s remote=%s", cfg::kCurrentFwVersion, remoteVersion.c_str());
    return false;
  }

  log.appendf("[OTA] new fw: %s -> %s\n", cfg::kCurrentFwVersion, remoteVersion.c_str());
  ESP_LOGI(TAG, "new firmware available: %s -> %s", cfg::kCurrentFwVersion, remoteVersion.c_str());

  // Step 3: Download and flash with retry
  for (int attempt = 1; attempt <= cfg::kOtaMaxAttempts; ++attempt) {
    log.appendf("[OTA] download attempt %d/%d\n", attempt, cfg::kOtaMaxAttempts);
    ESP_LOGI(TAG, "OTA attempt %d/%d", attempt, cfg::kOtaMaxAttempts);

    if (performOta(binUrl, log)) {
      log.appendf("[OTA] update OK, pending reboot\n");
      ESP_LOGI(TAG, "OTA success, pending reboot");
      return true;
    }

    ESP_LOGW(TAG, "OTA attempt %d failed", attempt);
    log.appendf("[OTA] attempt %d failed\n", attempt);
  }

  log.appendf("[OTA] all %d attempts failed, aborting\n", cfg::kOtaMaxAttempts);
  ESP_LOGE(TAG, "OTA failed after %d attempts", cfg::kOtaMaxAttempts);
  return false;
}

bool OtaService::fetchAndParseVersion(const std::string& versionUrl, std::string& remoteVersion, LogBuffer& log) {
  std::string body;
  if (!ServerApi::fetchFirmwareVersionJson(body, log)) {
    return false;
  }

  ESP_LOGI(TAG, "version json: %.*s", (int)body.size(), body.c_str());

  // Parse JSON: {"version": "x.y.z"}
  cJSON* root = cJSON_Parse(body.c_str());
  if (!root) {
    ESP_LOGE(TAG, "JSON parse failed");
    log.appendf("[OTA] JSON parse error\n");
    return false;
  }

  cJSON* ver = cJSON_GetObjectItem(root, "version");
  if (!ver || !cJSON_IsString(ver) || !ver->valuestring) {
    ESP_LOGE(TAG, "missing 'version' field");
    log.appendf("[OTA] missing version field\n");
    cJSON_Delete(root);
    return false;
  }

  remoteVersion = ver->valuestring;
  cJSON_Delete(root);

  log.appendf("[OTA] remote version: %s\n", remoteVersion.c_str());
  return true;
}

bool OtaService::needsUpdate(const std::string& remoteVersion) {
  return remoteVersion != cfg::kCurrentFwVersion;
}

bool OtaService::performOta(const std::string& binUrl, LogBuffer& log) {
  // Check OTA partition availability
  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition) {
    ESP_LOGE(TAG, "no OTA partition available");
    log.appendf("[OTA] no OTA partition\n");
    return false;
  }

  ESP_LOGI(TAG, "OTA target partition: %s (offset=0x%lx, size=0x%lx)",
           updatePartition->label, updatePartition->address, updatePartition->size);
  log.appendf("[OTA] target: %s\n", updatePartition->label);

  // Configure HTTP client for firmware download
  esp_http_client_config_t httpCfg{};
  httpCfg.url = binUrl.c_str();
  httpCfg.timeout_ms = (int)cfg::kOtaHttpTimeoutMs;
  httpCfg.keep_alive_enable = true;

  // Configure OTA
  esp_https_ota_config_t otaCfg{};
  otaCfg.http_config = &httpCfg;

  // Begin OTA
  esp_https_ota_handle_t otaHandle = nullptr;
  esp_err_t err = esp_https_ota_begin(&otaCfg, &otaHandle);
  if (err != ESP_OK || !otaHandle) {
    ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
    log.appendf("[OTA] begin failed: %s\n", esp_err_to_name(err));
    return false;
  }

  // Perform OTA (download + write in chunks, managed by esp_https_ota)
  int imageSize = esp_https_ota_get_image_size(otaHandle);
  ESP_LOGI(TAG, "firmware image size: %d bytes", imageSize);
  log.appendf("[OTA] image size: %d B\n", imageSize);

  while (true) {
    err = esp_https_ota_perform(otaHandle);
    if (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      int readSoFar = esp_https_ota_get_image_len_read(otaHandle);
      ESP_LOGD(TAG, "OTA progress: %d / %d bytes", readSoFar, imageSize);
      continue;
    }
    break;
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_https_ota_perform failed: %s", esp_err_to_name(err));
    log.appendf("[OTA] download failed: %s\n", esp_err_to_name(err));
    esp_https_ota_abort(otaHandle);
    return false;
  }

  // Verify and finish
  if (!esp_https_ota_is_complete_data_received(otaHandle)) {
    ESP_LOGE(TAG, "incomplete OTA data");
    log.appendf("[OTA] incomplete data\n");
    esp_https_ota_abort(otaHandle);
    return false;
  }

  err = esp_https_ota_finish(otaHandle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(err));
    log.appendf("[OTA] finish failed: %s\n", esp_err_to_name(err));
    return false;
  }

  // esp_https_ota_finish already sets the boot partition on success
  ESP_LOGI(TAG, "OTA complete, boot partition updated");
  return true;
}
