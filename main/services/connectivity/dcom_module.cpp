#include "services/connectivity/dcom_module.hpp"
#include "modules/io/io_controller.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "Dcom";

bool DcomModule::powerOn(LogBuffer& log) {
  log.appendf("[DCOM] power on\n");
  IoController::instance().setDcomPower(true);
  vTaskDelay(pdMS_TO_TICKS(1500));
  return true;
}

bool DcomModule::checkInternet(uint32_t timeoutMs, LogBuffer& log) {
  // TODO: implement real Wi-Fi join/check, or ping via esp_netif.
  // Placeholder: always fail unless you implement Wi-Fi.
  log.appendf("[DCOM] checkInternet=%d (stub: false)\n", 0);
  vTaskDelay(pdMS_TO_TICKS(timeoutMs/3));
  return false;
}

bool DcomModule::sendPayload(const std::string& json, LogBuffer& log) {
  log.appendf("[DCOM] sendPayload bytes=%d (stub)\n", (int)json.size());
  ESP_LOGI(TAG, "payload: %.*s", (int)json.size(), json.c_str());
  return true;
}

void DcomModule::powerOff(LogBuffer& log) {
  log.appendf("[DCOM] power off\n");
  IoController::instance().setDcomPower(false);
}
