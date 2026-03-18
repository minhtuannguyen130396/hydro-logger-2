#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "modules/sensor/sensor_manager.hpp"
#include "modules/rtc/rtc_pcf8563.hpp"
#include "services/logging/log_service.hpp"
#include "middleware/publish_api.hpp"
#include "board/adc_drv.hpp"
#include "app/app_state.hpp"
#include "app/app_context.hpp"
#include <algorithm>
#include <cstdio>

static const char* TAG = "MeasureTask";

extern "C" void measure_task_entry(void* arg) {
  auto* ctx = reinterpret_cast<AppContext*>(arg);

  // Register our handle for scheduler
  extern void register_task_handles(TaskHandle_t, TaskHandle_t, TaskHandle_t);
  // scheduler registers all; in this skeleton we register only when tasks start.
  // We'll let sync/ota tasks also call this and overwrite; it's fine for skeleton.
  register_task_handles(xTaskGetCurrentTaskHandle(), nullptr, nullptr);

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    LogBuffer log = LogService::createSessionLog();
    log.appendf("[Measure] start\n");

    DateTime now{};
    RtcPcf8563::instance().getTime(now);

    MeasurementMsg mm{};
    mm.time = now;
    mm.meta.voltage_mv = AdcDrv::readMilliVolts();

    LogMsg lm{};
    lm.time = now;
    lm.meta = mm.meta;

    ISensor* active = nullptr;
    if (!SensorManager::instance().ensureReady(active, log)) {
      ctx->state.set(AppState::BIT_LAST_MEASURE_FAIL);

      // log msg
      std::snprintf(lm.text, sizeof(lm.text), "%s", log.c_str());
      lm.len = (uint16_t)std::min(log.size(), (int)sizeof(lm.text)-1);
      PublishApi::publishLog(lm);

      mm.valid = false;
      PublishApi::publishMeasurement(mm);
      ESP_LOGW(TAG, "sensor warmup fail");
      continue;
    }

    int out[cfg::kDistanceSamples]{};
    bool ok = SensorManager::instance().read3(active, out, log);
    active->finishMeasurement(log);
    if (!ok) {
      ctx->state.set(AppState::BIT_LAST_MEASURE_FAIL);
      mm.valid = false;
    } else {
      ctx->state.clear(AppState::BIT_LAST_MEASURE_FAIL);
      mm.valid = true;
      for (int i = 0; i < cfg::kDistanceSamples; ++i) mm.dist_mm[i] = out[i];
    }

    // publish measurement
    PublishApi::publishMeasurement(mm);

    // publish log (1024B)
    std::snprintf(lm.text, sizeof(lm.text), "%s", log.c_str());
    lm.len = (uint16_t)std::min(log.size(), (int)sizeof(lm.text)-1);
    PublishApi::publishLog(lm);

    ESP_LOGI(TAG, "measure done valid=%d d=[%d,%d,%d]",
             (int)mm.valid, mm.dist_mm[0], mm.dist_mm[1], mm.dist_mm[2]);
  }
}
