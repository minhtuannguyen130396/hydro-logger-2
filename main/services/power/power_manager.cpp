#include "services/power/power_manager.hpp"
#include "modules/io/io_controller.hpp"
#include "esp_log.h"
#include "esp_wifi.h"

static const char* TAG = "Power";

void PowerManager::enterSafeMode() {
  if (in_safe_mode_) return;

  auto& io = IoController::instance();
  io.setLaserPower(false);
  io.setUltrasonicPower(false);
  io.setSimPower(false);
  io.setDcomPower(false);
  io.setSpeaker(false);

  // Stop Wi-Fi radio if running
  esp_wifi_disconnect();
  esp_wifi_stop();

  in_safe_mode_ = true;
  ESP_LOGI(TAG, "entered safe mode (peripherals off)");
}

void PowerManager::exitSafeMode() {
  if (!in_safe_mode_) return;

  in_safe_mode_ = false;
  ESP_LOGI(TAG, "exited safe mode");
}
