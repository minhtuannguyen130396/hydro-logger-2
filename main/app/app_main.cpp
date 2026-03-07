#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "common/nvs_store.hpp"
#include "middleware/message_bus.hpp"
#include "middleware/publish_api.hpp"

#include "modules/io/io_controller.hpp"
#include "modules/rtc/rtc_ds1307.hpp"
#include "board/adc_drv.hpp"

#include "app/app_state.hpp"
#include "app/app_context.hpp"

// task entry functions
extern "C" void scheduler_task_entry(void* arg);
extern "C" void measure_task_entry(void* arg);
extern "C" void sync_task_entry(void* arg);
extern "C" void notify_task_entry(void* arg);
extern "C" void ota_task_entry(void* arg);

static const char* TAG = "AppMain";

static AppContext g_ctx;

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "boot");

  NvsStore::init();

  IoController::instance().init();
  AdcDrv::init();
  RtcDs1307::instance().init();

  g_ctx.bus.init();
  g_ctx.state.init();
  PublishApi::setBus(&g_ctx.bus);

  // Create tasks
  xTaskCreate(&measure_task_entry, "measure_task", 6144, &g_ctx, 6, nullptr);
  xTaskCreate(&sync_task_entry,    "sync_task",    8192, &g_ctx, 5, nullptr);
  xTaskCreate(&ota_task_entry,     "ota_task",     8192, &g_ctx, 4, nullptr);
  xTaskCreate(&notify_task_entry,  "notify_task",  4096, &g_ctx, 3, nullptr);
  xTaskCreate(&scheduler_task_entry,"scheduler",   4096, &g_ctx, 7, nullptr);

  ESP_LOGI(TAG, "tasks created");
}
