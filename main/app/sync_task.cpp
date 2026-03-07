#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app/app_state.hpp"
#include "app/app_context.hpp"
#include "modules/rtc/rtc_ds1307.hpp"
#include "services/connectivity/connectivity_manager.hpp"
#include "services/logging/log_service.hpp"
#include "services/pack/json_packer.hpp"
#include "services/net/server_api.hpp"
#include "board/adc_drv.hpp"
#include "common/config.hpp"
#include "common/time_utils.hpp"

static const char* TAG = "SyncTask";

extern "C" void sync_task_entry(void* arg) {
  auto* ctx = reinterpret_cast<AppContext*>(arg);

  extern void register_task_handles(TaskHandle_t, TaskHandle_t, TaskHandle_t);
  register_task_handles(nullptr, xTaskGetCurrentTaskHandle(), nullptr);

  ConnectivityManager cm;

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    DateTime now{};
    if (!RtcDs1307::instance().getTime(now)) {
      ESP_LOGW(TAG, "RTC read fail");
      continue;
    }
    if (!timeu::isSyncMinute(now.minute)) {
      ESP_LOGI(TAG, "minute=%d !=0 -> skip sync", now.minute);
      continue;
    }

    ctx->state.set(AppState::BIT_SYNC_RUNNING);
    LogBuffer log = LogService::createSessionLog();
    log.appendf("[Sync] start\n");

    bool conn_ok = cm.warmup(log);
    if (!conn_ok) {
      ctx->state.clear(AppState::BIT_CONN_OK);
      ctx->state.set(AppState::BIT_CONN_FAIL | AppState::BIT_LAST_SYNC_FAIL);
      ctx->state.clear(AppState::BIT_SYNC_RUNNING);
      ESP_LOGW(TAG, "connectivity warmup failed");
      continue;
    }

    ctx->state.set(AppState::BIT_CONN_OK);
    ctx->state.clear(AppState::BIT_CONN_FAIL);

    // 1-minute send window
    uint32_t start = (uint32_t)xTaskGetTickCount();
    int sent_meas = 0, sent_log = 0;

    while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < cfg::kSyncWindowMs) {
      MeasurementMsg mm{};
      LogMsg lm{};
      bool did = false;

      if (ctx->bus.popMeasurement(mm, cfg::kQueuePopTimeoutMs)) {
        mm.meta.voltage_mv = AdcDrv::readMilliVolts();
        std::string json = JsonPacker::packMeasurement(mm);

        bool ok = false;
        if (cm.active() && cm.active()->type() == CommType::Sim4G) {
          ok = cm.active()->sendPayload(json, log);
        } else {
          ok = ServerApi::sendMeasurement(json, log);
        }
        log.appendf("[Sync] meas send=%d\n", (int)ok);
        sent_meas += ok ? 1 : 0;
        did = true;
      }

      if (ctx->bus.popLog(lm, cfg::kQueuePopTimeoutMs)) {
        lm.meta.voltage_mv = AdcDrv::readMilliVolts();
        std::string json = JsonPacker::packLog(lm);

        bool ok = false;
        if (cm.active() && cm.active()->type() == CommType::Sim4G) {
          ok = cm.active()->sendPayload(json, log);
        } else {
          ok = ServerApi::sendLog(json, log);
        }
        log.appendf("[Sync] log send=%d\n", (int)ok);
        sent_log += ok ? 1 : 0;
        did = true;
      }

      if (!did) vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "sync done meas=%d log=%d", sent_meas, sent_log);
    ctx->state.clear(AppState::BIT_LAST_SYNC_FAIL);
    ctx->state.clear(AppState::BIT_SYNC_RUNNING);
  }
}
