#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app/app_state.hpp"
#include "app/app_context.hpp"
#include "services/ota/ota_service.hpp"
#include "services/connectivity/connectivity_manager.hpp"
#include "services/logging/log_service.hpp"
#include "common/config.hpp"

static const char* TAG = "OtaTask";

extern "C" void ota_task_entry(void* arg) {
  auto* ctx = reinterpret_cast<AppContext*>(arg);

  extern void register_task_handles(TaskHandle_t, TaskHandle_t, TaskHandle_t);
  register_task_handles(nullptr, nullptr, xTaskGetCurrentTaskHandle());

  OtaService ota;
  ConnectivityManager conn;

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    ctx->state.set(AppState::BIT_OTA_RUNNING);
    LogBuffer log = LogService::createSessionLog();
    log.appendf("[OTA] start\n");

    // Step 1: Check connectivity — OTA only via DCOM (Wi-Fi)
    log.appendf("[OTA] checking connectivity\n");
    if (!conn.warmup(log)) {
      ESP_LOGW(TAG, "no connectivity, skip OTA");
      log.appendf("[OTA] no network, skip\n");
      ctx->state.clear(AppState::BIT_OTA_RUNNING);
      continue;
    }

    if (conn.active()->type() != CommType::Dcom) {
      ESP_LOGW(TAG, "connected via SIM, OTA requires DCOM (Wi-Fi), skip");
      log.appendf("[OTA] SIM active, OTA needs DCOM, skip\n");
      ctx->state.clear(AppState::BIT_OTA_RUNNING);
      continue;
    }

    // Step 2: Run OTA check and update
    bool ok = ota.checkAndUpdate(cfg::kFirmwareVersionUrl, cfg::kFirmwareBinUrl, log);
    ESP_LOGI(TAG, "ota result=%d", (int)ok);

    if (ok) {
      // OTA succeeded - restart to boot into new firmware
      log.appendf("[OTA] restarting...\n");
      ESP_LOGI(TAG, "OTA success, restarting in 2s...");
      vTaskDelay(pdMS_TO_TICKS(2000));
      esp_restart();
    }

    ctx->state.clear(AppState::BIT_OTA_RUNNING);
  }
}
