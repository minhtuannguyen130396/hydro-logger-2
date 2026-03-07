#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app/app_state.hpp"
#include "app/app_context.hpp"
#include "services/ota/ota_service.hpp"
#include "services/logging/log_service.hpp"
#include "common/config.hpp"

static const char* TAG = "OtaTask";

extern "C" void ota_task_entry(void* arg) {
  auto* ctx = reinterpret_cast<AppContext*>(arg);

  extern void register_task_handles(TaskHandle_t, TaskHandle_t, TaskHandle_t);
  register_task_handles(nullptr, nullptr, xTaskGetCurrentTaskHandle());

  OtaService ota;

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    ctx->state.set(AppState::BIT_OTA_RUNNING);
    LogBuffer log = LogService::createSessionLog();
    log.appendf("[OTA] start\n");

    bool ok = ota.checkAndUpdate(cfg::kFirmwareVersionUrl, cfg::kFirmwareBinUrl, log);
    ESP_LOGI(TAG, "ota check done ok=%d", (int)ok);

    ctx->state.clear(AppState::BIT_OTA_RUNNING);
  }
}
