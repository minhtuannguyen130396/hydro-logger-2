#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app/app_state.hpp"
#include "app/app_context.hpp"

static const char* TAG = "OtaTask";

// OTA logic has been merged into sync_task to avoid duplicate powerOn.
// This task is kept as a stub so app_main task creation doesn't need changes.
extern "C" void ota_task_entry(void* arg) {
  (void)arg;

  extern void register_task_handles(TaskHandle_t, TaskHandle_t, TaskHandle_t);
  register_task_handles(nullptr, nullptr, xTaskGetCurrentTaskHandle());

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "OTA handled by SyncTask, ignoring notify");
  }
}
