#include "services/ota/ota_service.hpp"
#include "services/net/server_api.hpp"
#include "esp_log.h"

static const char* TAG = "OTA";

bool OtaService::checkAndUpdate(const std::string& versionUrl, const std::string& binUrl, LogBuffer& log) {
  // TODO: implement real OTA with esp_https_ota / esp_ota_* API.
  // Here we only fetch version json as a stub.
  (void)versionUrl;
  (void)binUrl;

  std::string ver;
  if (!ServerApi::fetchFirmwareVersionJson(ver, log)) {
    log.appendf("[OTA] version fetch failed\n");
    return false;
  }
  ESP_LOGI(TAG, "version json: %.*s", (int)ver.size(), ver.c_str());
  log.appendf("[OTA] check done (stub, no update)\n");
  return true;
}
