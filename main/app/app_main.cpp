#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "common/nvs_store.hpp"
#include "middleware/message_bus.hpp"
#include "middleware/publish_api.hpp"

#include "modules/io/io_controller.hpp"
#include "modules/rtc/rtc_pcf8563.hpp"
#include "board/adc_drv.hpp"

#include "app/app_state.hpp"
#include "app/app_context.hpp"

// diagnostic (runs once at boot, then self-deletes)
extern void diagnostic_run_blocking();

// runtime config console: reads "serial_number:TD_MW_0012" etc. from UART0
extern void config_console_start();

// task entry functions
extern "C" void scheduler_task_entry(void* arg);
extern "C" void measure_task_entry(void* arg);
extern "C" void sync_task_entry(void* arg);
extern "C" void notify_task_entry(void* arg);
extern "C" void ota_task_entry(void* arg);
extern "C" void failover_task_entry(void* arg);

static const char* TAG = "AppMain";

static AppContext g_ctx;

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "boot");

  // OTA rollback: mark current firmware as valid after successful boot.
  // If a previous OTA update failed to boot, the bootloader will
  // automatically rollback to the previous working firmware.
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t otaState;
  if (esp_ota_get_state_partition(running, &otaState) == ESP_OK) {
    if (otaState == ESP_OTA_IMG_PENDING_VERIFY) {
      ESP_LOGI(TAG, "OTA: first boot after update, marking firmware valid");
      esp_ota_mark_app_valid_cancel_rollback();
    }
  }

  if (!NvsStore::init()) {
    ESP_LOGI(TAG, "NVS init failed");
  }

  IoController::instance().init();
  AdcDrv::init();
  RtcPcf8563::instance().init();

  if (!g_ctx.bus.init()) {
    ESP_LOGI(TAG, "MessageBus init failed");
  }
  g_ctx.state.init();
  g_ctx.failover_q = xQueueCreate(1, sizeof(FailoverRequest));
  if (!g_ctx.failover_q) {
    ESP_LOGI(TAG, "Failover queue init failed");
  }
  PublishApi::setBus(&g_ctx.bus);

  // Runtime config console (UART0). Start it before the diagnostic so the
  // operator can type "serial_number:TD_MW_0012" during the boot/diagnostic
  // window to persist the device code to NVS.
  config_console_start();

  // Wake cause decides the boot path:
  //  - cold boot (power-on / reset / brownout = UNDEFINED): run the full boot
  //    diagnostic (self-test + connectivity + active data window).
  //  - RTC alarm (EXT0): we were woken to take a scheduled measurement, so skip
  //    the diagnostic and go straight into the measure flow. Clear the alarm
  //    flag first so the RTC releases the INT line (held low until acknowledged).
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_EXT0) {
    ESP_LOGI(TAG, "woke from RTC alarm (EXT0), skipping diagnostic");
    RtcPcf8563::instance().clearAlarmFlag();
  } else {
    // Boot diagnostic: test all modules, then wait kDiagnosticDelayMs
    diagnostic_run_blocking();
    ESP_LOGI(TAG, "diagnostic complete, starting main tasks");
  }

  // Create tasks. measure/sync handles are captured so the orchestrator
  // (scheduler) can notify them directly.
  xTaskCreate(&measure_task_entry, "measure_task", 6144, &g_ctx, 6, &g_ctx.measure_h);
  xTaskCreate(&sync_task_entry,    "sync_task",    8192, &g_ctx, 5, &g_ctx.sync_h);
  xTaskCreate(&ota_task_entry,     "ota_task",     8192, &g_ctx, 4, nullptr);
  xTaskCreate(&notify_task_entry,  "notify_task",  4096, &g_ctx, 3, nullptr);
  xTaskCreate(&failover_task_entry,"failover_task",4096, &g_ctx, 3, nullptr);
  xTaskCreate(&scheduler_task_entry,"scheduler",   4096, &g_ctx, 7, nullptr);

  ESP_LOGI(TAG, "tasks created");
}
