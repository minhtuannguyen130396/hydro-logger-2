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

// Continuous wake-cycle orchestrator.
//
// OLD behaviour (deep sleep): each boot ran exactly one measure cycle, then
// called esp_deep_sleep_start() which reset the chip.
//
// NEW behaviour (light sleep): the scheduler runs in an infinite loop.
// After finishing the cycle's work it:
//   1. Powers down all peripherals and tears down Wi-Fi completely.
//   2. Programs the next RTC minute-alarm.
//   3. Enters **light sleep** with two wakeup sources:
//        a. EXT0 on pins::WAKEUP (RTC alarm INT active-low).
//        b. Timer of cfg::kLightSleepTimeoutMs (15 min default).
//   4. On wake the scheduler checks the wake reason:
//        - RTC alarm  → normal cycle: measure; sync only if top-of-hour.
//        - Timeout    → measure + immediate sync/post (no hour-gate).
//   Light sleep does NOT reset the chip, so all FreeRTOS tasks and queues
//   survive across sleeps.
extern "C" void scheduler_task_entry(void* arg) {
  auto* ctx = reinterpret_cast<AppContext*>(arg);
  PowerManager pm;

  // ---------- first-cycle: triggered from app_main (tasks already created) --
  // The first iteration falls straight through to the work section below.
  // Subsequent iterations begin after waking from light sleep.

  bool first_cycle = true;
  bool force_sync  = false;   // set true when woken by timeout

  while (true) {
    // ===== Read current RTC time =====
    DateTime now{};
    if (!RtcPcf8563::instance().getTime(now)) {
      ESP_LOGW(TAG, "RTC read fail (check I2C/PCF8563), using defaults");
    }

    // ===== Decide what work to do this cycle =====
    pm.exitSafeMode();  // allow peripherals to power up for measure/sync

    // On timeout-wake we always sync (measure + post immediately).
    // On RTC-alarm-wake we only sync at the top of the hour (minute == 0).
    const bool sync_expected = force_sync || timeu::isSyncMinute(now.minute);

    ESP_LOGI(TAG, "wake cycle at %02d:%02d -> measure%s%s",
             now.hour, now.minute,
             sync_expected ? " + sync" : "",
             force_sync    ? " (timeout-forced)" : "");

    // ===== Trigger this cycle's work =====
    if (ctx->measure_h) xTaskNotifyGive(ctx->measure_h);
    if (sync_expected && ctx->sync_h) xTaskNotifyGive(ctx->sync_h);

    // Give the notified tasks a moment to set their RUNNING bits before we
    // start polling for completion (this task has the highest priority).
    vTaskDelay(pdMS_TO_TICKS(100));

    // ===== Wait for work to complete, capped by the awake budget =====
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

    // ===== Program the next RTC wake alarm (re-read time to minimise slip) =
    RtcPcf8563::instance().getTime(now);
    const int next_min = timeu::nextScheduledMinute(now.minute);
    // setMinuteAlarm clears AF, releasing the INT line high until the alarm fires.
    RtcPcf8563::instance().setMinuteAlarm((uint8_t)next_min);
    ESP_LOGI(TAG, "cycle done at %02d:%02d, next alarm minute=%02d",
             now.hour, now.minute, next_min);

    // ===== Power down and enter light sleep =====
    pm.enterSafeMode();  // peripherals off, Wi-Fi fully deinited

    WakeReason reason = pm.enterLightSleep(pins::WAKEUP,
                                           cfg::kLightSleepTimeoutMs);

    // ===== Post-wake bookkeeping =====
    if (reason == WakeReason::RtcAlarm) {
      // Delay 1s để bộ đếm RTC chắc chắn nhảy hẳn sang phút mới (giây >= 01),
      // chống jitter hoặc đọc trúng ranh giới giây 59.
      vTaskDelay(pdMS_TO_TICKS(1000));
      RtcPcf8563::instance().clearAlarmFlag();
      force_sync = false;
    } else if (reason == WakeReason::Timeout) {
      // Timeout wake → measure + post immediately on the next iteration
      force_sync = true;
      ESP_LOGI(TAG, "timeout wake -> will force sync this cycle");
    } else {
      force_sync = false;
    }
    first_cycle = false;
  }
}
