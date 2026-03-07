#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "modules/rtc/rtc_ds1307.hpp"
#include "common/time_utils.hpp"
#include "common/config.hpp"
#include "services/power/power_manager.hpp"

static const char* TAG = "Scheduler";

static TaskHandle_t g_measure_h = nullptr;
static TaskHandle_t g_sync_h    = nullptr;
static TaskHandle_t g_ota_h     = nullptr;

extern "C" void register_task_handles(TaskHandle_t measure, TaskHandle_t sync, TaskHandle_t ota) {
  if (measure) g_measure_h = measure;
  if (sync)    g_sync_h = sync;
  if (ota)     g_ota_h = ota;
}

static void notify(TaskHandle_t h) {
  if (h) xTaskNotifyGive(h);
}

extern "C" void scheduler_task_entry(void* arg) {
  (void)arg;
  PowerManager pm;

  int last_trigger_minute = -1;
  while (true) {
    DateTime now{};
    bool ok = RtcDs1307::instance().getTime(now);
    if (!ok) {
      ESP_LOGW(TAG, "RTC read fail (check I2C/DS1307)");
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    const bool scheduled = timeu::isScheduledMinute(now.minute);
    if (scheduled && now.second == 0 && now.minute != last_trigger_minute) {
      last_trigger_minute = now.minute;
      ESP_LOGI(TAG, "tick at %02d:%02d -> trigger measure", now.hour, now.minute);
      pm.exitSafeMode();
      notify(g_measure_h);

      if (timeu::isSyncMinute(now.minute)) {
        ESP_LOGI(TAG, "minute==0 -> trigger sync");
        notify(g_sync_h);
      }

      // OTA check after measure+sync in same 10-min cycle
      notify(g_ota_h);
    }

    if (!scheduled) {
      pm.enterSafeMode();
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
