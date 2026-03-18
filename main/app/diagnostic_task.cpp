#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "modules/rtc/rtc_pcf8563.hpp"
#include "modules/sensor/sensor_manager.hpp"
#include "modules/io/io_controller.hpp"
#include "services/connectivity/connectivity_manager.hpp"
#include "services/logging/log_service.hpp"
#include "board/adc_drv.hpp"
#include "common/config.hpp"

static const char* TAG = "Diagnostic";

static SemaphoreHandle_t s_diag_done = nullptr;

static void test_rtc() {
  DateTime now{};
  bool ok = RtcPcf8563::instance().getTime(now);
  if (ok) {
    ESP_LOGI(TAG, "[RTC] OK -> %04d-%02d-%02d %02d:%02d:%02d",
             now.year, now.month, now.day, now.hour, now.minute, now.second);
  } else {
    ESP_LOGW(TAG, "[RTC] FAIL (I2C or PCF8563 not connected)");
  }
}

static void test_adc() {
  int mv = AdcDrv::readMilliVolts();
  ESP_LOGI(TAG, "[ADC] voltage = %d mV", mv);
}

static void test_sensor() {
  LogBuffer log = LogService::createSessionLog();
  SensorManager& sm = SensorManager::instance();
  ISensor* active = nullptr;

  if (sm.ensureReady(active, log)) {
    int mm = 0;
    if (active->readDistanceMm(mm, log)) {
      ESP_LOGI(TAG, "[Sensor] OK type=%s dist=%d mm",
               active->type() == SensorType::Laser ? "Laser" : "Ultrasonic", mm);
    } else {
      ESP_LOGW(TAG, "[Sensor] read FAIL (warmup OK but read failed)");
    }
  } else {
    ESP_LOGW(TAG, "[Sensor] FAIL (warmup failed or no sensor selected)");
  }
}

static void test_connectivity() {
  LogBuffer log = LogService::createSessionLog();
  ConnectivityManager cm;

  ESP_LOGI(TAG, "[Conn] testing SIM + DCOM ...");
  bool ok = cm.warmup(log);
  if (ok) {
    const char* name = cm.active()->type() == CommType::Sim4G ? "SIM4G" : "DCOM";
    ESP_LOGI(TAG, "[Conn] OK -> active=%s", name);
    cm.active()->powerOff(log);
  } else {
    ESP_LOGW(TAG, "[Conn] FAIL (both SIM and DCOM failed)");
  }
}

static void diagnostic_task_fn(void* arg) {
  ESP_LOGI(TAG, "========== BOOT DIAGNOSTIC START ==========");

  test_rtc();
  test_adc();
  test_sensor();
  test_connectivity();

  ESP_LOGI(TAG, "========== BOOT DIAGNOSTIC END ==========");
  ESP_LOGI(TAG, "waiting %lu ms before starting main tasks...",
           (unsigned long)cfg::kDiagnosticDelayMs);
  vTaskDelay(pdMS_TO_TICKS(cfg::kDiagnosticDelayMs));

  // Signal app_main that diagnostic is complete
  xSemaphoreGive(s_diag_done);

  // Self-delete
  vTaskDelete(nullptr);
}

void diagnostic_run_blocking() {
  s_diag_done = xSemaphoreCreateBinary();

  xTaskCreate(&diagnostic_task_fn, "diagnostic", 8192, nullptr, 8, nullptr);

  // Block until diagnostic task signals completion
  xSemaphoreTake(s_diag_done, portMAX_DELAY);
  vSemaphoreDelete(s_diag_done);
  s_diag_done = nullptr;
}
