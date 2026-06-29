#include "services/power/power_manager.hpp"
#include "modules/io/io_controller.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

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

void PowerManager::enterDeepSleep(gpio_num_t wake_pin) {
  // Make sure the status LED isn't latched on through sleep.
  IoController::instance().setLed(false);

  // PCF8563 INT is open-drain active-low; idle-high needs a pull-up. Enable the
  // internal RTC pull-up (external pull-up on the board is preferred) and keep
  // the RTC peripheral domain powered so the pull-up holds during deep sleep.
  rtc_gpio_pullup_en(wake_pin);
  rtc_gpio_pulldown_dis(wake_pin);
  esp_sleep_enable_ext0_wakeup(wake_pin, 0);  // wake when INT is pulled low
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  // NOTE: during deep sleep the peripheral-enable pins revert to high-Z. The
  // board's external pull resistors must hold them in the off state set by
  // enterSafeMode(). If sleep current is too high, add per-pin gpio_hold_en()
  // here (and gpio_hold_dis() early in app_main) to latch those levels.
  ESP_LOGI(TAG, "entering deep sleep, wake on GPIO%d low", (int)wake_pin);
  esp_deep_sleep_start();  // never returns
}
