#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "modules/rtc/rtc_pcf8563.hpp"
#include "board/pins.hpp"
#include "common/time_utils.hpp"
#include "common/config.hpp"
#include "services/power/power_manager.hpp"
#include "app/app_state.hpp"
#include "app/app_context.hpp"

static const char* TAG = "Scheduler";

// Single wake-cycle orchestrator: trigger the scheduled work (measure, plus
// sync at the top of the hour), wait for it to finish, program the next RTC
// alarm, then deep sleep. Deep sleep resets the chip, so each boot runs exactly
// one cycle through this function — it never returns.
extern "C" void scheduler_task_entry(void* arg) {
  auto* ctx = reinterpret_cast<AppContext*>(arg);
  PowerManager pm;

  DateTime now{};
  if (!RtcPcf8563::instance().getTime(now)) {
    ESP_LOGW(TAG, "RTC read fail (check I2C/PCF8563), using defaults");
  }

  // --- Trigger this cycle's work ---
  pm.exitSafeMode();  // allow peripherals to power up for measure/sync

  const bool sync_expected = timeu::isSyncMinute(now.minute);
  ESP_LOGI(TAG, "wake cycle at %02d:%02d -> measure%s",
           now.hour, now.minute, sync_expected ? " + sync" : "");

  if (ctx->measure_h) xTaskNotifyGive(ctx->measure_h);
  if (sync_expected && ctx->sync_h) xTaskNotifyGive(ctx->sync_h);

  // Give the notified tasks a moment to set their RUNNING bits before we start
  // polling for completion (this task has the highest priority).
  vTaskDelay(pdMS_TO_TICKS(100));

  // --- Wait for work to complete, capped by the awake budget ---
  // Include BIT_OTA_RUNNING so the scheduler does not force deep sleep while
  // an OTA download is in progress (a reset mid-flash corrupts the update).
  const EventBits_t busy_mask =
      AppState::BIT_MEASURE_RUNNING | AppState::BIT_SYNC_RUNNING | AppState::BIT_OTA_RUNNING;
  const uint32_t start = (uint32_t)xTaskGetTickCount();
  while (true) {
    EventBits_t bits = ctx->state.get();
    if ((bits & busy_mask) == 0) break;

    // Use a longer budget while OTA is running to avoid killing the download.
    uint32_t budget = (bits & AppState::BIT_OTA_RUNNING)
                        ? cfg::kOtaAwakeBudgetMs
                        : cfg::kAwakeBudgetMs;
    if (pdTICKS_TO_MS(xTaskGetTickCount() - start) >= budget) {
      ESP_LOGW(TAG, "awake budget %ums exceeded, sleeping anyway",
               (unsigned)budget);
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  // --- Program the next wake alarm (re-read time to minimize slip) ---
  RtcPcf8563::instance().getTime(now);
  const int next_min = timeu::nextScheduledMinute(now.minute);
  // setMinuteAlarm clears AF, releasing the INT line high until the alarm fires.
  RtcPcf8563::instance().setMinuteAlarm((uint8_t)next_min);
  ESP_LOGI(TAG, "cycle done at %02d:%02d, next alarm minute=%02d",
           now.hour, now.minute, next_min);

  // --- Power down and sleep until the RTC pulls INT low on GPIO33 ---
  pm.enterSafeMode();
  pm.enterDeepSleep(pins::WAKEUP);  // never returns
}
