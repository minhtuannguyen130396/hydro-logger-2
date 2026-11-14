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

  // Fully tear down Wi-Fi before sleeping
  shutdownWifi();

  in_safe_mode_ = true;
  ESP_LOGI(TAG, "entered safe mode (peripherals off)");
}

void PowerManager::exitSafeMode() {
  if (!in_safe_mode_) return;

  in_safe_mode_ = false;
  ESP_LOGI(TAG, "exited safe mode");
}

void PowerManager::shutdownWifi() {
  // Best-effort full teardown: disconnect -> stop.
  // We do NOT call esp_wifi_deinit() because light-sleep preserves memory,
  // and deinit breaks re-initialization on subsequent wake cycles.
  esp_wifi_disconnect();
  esp_wifi_stop();
  ESP_LOGI(TAG, "Wi-Fi stopped (disconnect + stop)");
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

WakeReason PowerManager::enterLightSleep(gpio_num_t wake_pin, uint32_t timeout_ms) {
  // Make sure the status LED isn't latched on through sleep.
  IoController::instance().setLed(false);

  // --- Configure EXT0 wakeup (RTC alarm INT, active-low) ---
  rtc_gpio_pullup_en(wake_pin);
  rtc_gpio_pulldown_dis(wake_pin);
  esp_sleep_enable_ext0_wakeup(wake_pin, 0);  // wake when INT is pulled low
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  // --- Configure timer wakeup ---
  esp_sleep_enable_timer_wakeup((uint64_t)timeout_ms * 1000ULL);  // µs

  ESP_LOGI(TAG, "entering light sleep, wake on GPIO%d low OR timer %ums",
           (int)wake_pin, (unsigned)timeout_ms);

  // --- Enter light sleep (blocks until wakeup) ---
  esp_light_sleep_start();

  // --- Determine wake cause ---
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  WakeReason reason;
  if (cause == ESP_SLEEP_WAKEUP_EXT0) {
    reason = WakeReason::RtcAlarm;
    ESP_LOGI(TAG, "woke from light sleep: RTC alarm (EXT0)");
  } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    reason = WakeReason::Timeout;
    ESP_LOGI(TAG, "woke from light sleep: timer timeout (%ums)", (unsigned)timeout_ms);
  } else {
    reason = WakeReason::Unknown;
    ESP_LOGW(TAG, "woke from light sleep: unknown cause %d", (int)cause);
  }

  // Disable wakeup sources so they don't interfere with future sleep calls
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);

  return reason;
}
