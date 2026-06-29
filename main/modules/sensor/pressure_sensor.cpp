#include "modules/sensor/pressure_sensor.hpp"

#include "board/adc_drv.hpp"
#include "board/pins.hpp"
#include "common/config.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modules/io/io_controller.hpp"

namespace {
static const char* TAG = "Pressure";
constexpr uint32_t kSettleDelayMs = 6000;  // let the analog rail settle before the first read
}  // namespace

bool PressureSensor::warmup(LogBuffer& log) {
  ESP_LOGI(TAG, "power ON (shared ULTRA_PWR), warmup %lu ms", (unsigned long)kSettleDelayMs);
  log.appendf("[Pressure] power on\n");
  IoController::instance().setUltrasonicPower(true);

  vTaskDelay(pdMS_TO_TICKS(kSettleDelayMs));

  int raw = AdcDrv::readLSignalRaw();
  ESP_LOGI(TAG, "warmup raw=%d (avg of %d samples)", raw, cfg::kPressureAdcSamples);
  log.appendf("[Pressure] warmup raw=%d\n", raw);
  return true;
}

void PressureSensor::finishMeasurement(LogBuffer& log) {
  ESP_LOGI(TAG, "power OFF (shared ULTRA_PWR)");
  log.appendf("[Pressure] power off\n");
  IoController::instance().setUltrasonicPower(false);
}

bool PressureSensor::readDistanceMm(int& outMm, LogBuffer& log) {
  int raw = AdcDrv::readLSignalRaw();
  if (raw <= 0) {
    ESP_LOGW(TAG, "read FAIL raw=%d", raw);
    log.appendf("[Pressure] read FAIL raw=%d\n", raw);
    return false;
  }

  // Report the averaged raw ADC count as the measured value; it is carried by
  // dist_mm[] into water_lever_0..2.
  outMm = raw;
  ESP_LOGI(TAG, "read raw=%d", raw);
  log.appendf("[Pressure] raw=%d\n", raw);
  return true;
}
