#include "services/power/power_manager.hpp"
#include "esp_log.h"

static const char* TAG = "Power";

void PowerManager::enterSafeMode() {
  // TODO: implement light sleep / deep sleep depending on your requirements.
  ESP_LOGI(TAG, "enterSafeMode (stub)");
}

void PowerManager::exitSafeMode() {
  ESP_LOGI(TAG, "exitSafeMode (stub)");
}
