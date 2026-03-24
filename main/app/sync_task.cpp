#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app/app_state.hpp"
#include "app/app_context.hpp"
#include "modules/rtc/rtc_pcf8563.hpp"
#include "services/connectivity/connectivity_manager.hpp"
#include "services/connectivity/sim4g_module.hpp"
#include "services/logging/log_service.hpp"
#include "services/pack/json_packer.hpp"
#include "services/net/server_api.hpp"
#include "services/ota/ota_service.hpp"
#include "board/adc_drv.hpp"
#include "common/config.hpp"
#include "common/time_utils.hpp"

static const char* TAG = "SyncTask";

// Retry-fetch helper: retry a fetch function within a deadline.
// On failure: warn, wait 2s, retry. After deadline: log error, return false.
static constexpr uint32_t kFetchTimeoutMs  = 20000;
static constexpr uint32_t kFetchRetryDelayMs = 2000;

// ============================================================
// Time sync with 20s retry window
// ============================================================
static bool syncTimeWithRetry(ICommModule* active, LogBuffer& log) {
  ESP_LOGI(TAG, "[TimeSync] start (timeout %dms)", (int)kFetchTimeoutMs);
  log.appendf("[Sync] time sync start (timeout %ds)\n", (int)(kFetchTimeoutMs / 1000));

  uint32_t start = (uint32_t)xTaskGetTickCount();

  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < kFetchTimeoutMs) {
    std::string timeStr;
    bool fetched = false;

    if (active && active->type() == CommType::Sim4G) {
      fetched = static_cast<Sim4GModule*>(active)->httpGet(
          ServerApi::timeUrl(), timeStr, log);
    } else {
      fetched = ServerApi::fetchServerTime(timeStr, log);
    }

    if (fetched) {
      DateTime serverTime{};
      if (timeu::parseServerTime(timeStr.c_str(), serverTime)) {
        DateTime rtcNow{};
        RtcPcf8563::instance().getTime(rtcNow);
        int64_t delta = timeu::deltaSeconds(serverTime, rtcNow);
        log.appendf("[Sync] time delta=%llds\n", (long long)delta);

        if (delta > 60) {
          RtcPcf8563::instance().setTime(serverTime);
          log.appendf("[Sync] RTC updated -> %04d-%02d-%02d %02d:%02d:%02d\n",
                      serverTime.year, serverTime.month, serverTime.day,
                      serverTime.hour, serverTime.minute, serverTime.second);
          ESP_LOGI(TAG, "RTC synced delta=%llds", (long long)delta);
        } else {
          log.appendf("[Sync] RTC OK (delta <= 60s)\n");
        }
        return true;
      }
      // Parse failed — still retry
      log.appendf("[Sync] time parse FAIL: '%s'\n", timeStr.c_str());
      ESP_LOGW(TAG, "[TimeSync] parse FAIL, retrying in %dms...", (int)kFetchRetryDelayMs);
    } else {
      ESP_LOGW(TAG, "[TimeSync] fetch FAIL, retrying in %dms...", (int)kFetchRetryDelayMs);
      log.appendf("[Sync] time fetch FAIL, retry in %ds\n", (int)(kFetchRetryDelayMs / 1000));
    }

    vTaskDelay(pdMS_TO_TICKS(kFetchRetryDelayMs));
  }

  ESP_LOGE(TAG, "[TimeSync] TIMEOUT after %dms", (int)kFetchTimeoutMs);
  log.appendf("[Sync] time sync TIMEOUT (%ds)\n", (int)(kFetchTimeoutMs / 1000));
  return false;
}

// ============================================================
// OTA version check with 20s retry window
// ============================================================
static bool otaCheckWithRetry(ICommModule* active, LogBuffer& log) {
  // OTA only via DCOM (Wi-Fi)
  if (!active || active->type() != CommType::Dcom) {
    ESP_LOGI(TAG, "[OTA] skip (needs DCOM, active=%s)",
             active ? "SIM4G" : "none");
    log.appendf("[OTA] skip (needs DCOM)\n");
    return false;
  }

  ESP_LOGI(TAG, "[OTA] version check start (timeout %dms)", (int)kFetchTimeoutMs);
  log.appendf("[OTA] version check start (timeout %ds)\n", (int)(kFetchTimeoutMs / 1000));

  OtaService ota;
  uint32_t start = (uint32_t)xTaskGetTickCount();

  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < kFetchTimeoutMs) {
    bool ok = ota.checkAndUpdate(cfg::kFirmwareVersionUrl, cfg::kFirmwareBinUrl, log);
    if (ok) {
      // OTA succeeded — restart
      log.appendf("[OTA] restarting...\n");
      ESP_LOGI(TAG, "OTA success, restarting in 2s...");
      vTaskDelay(pdMS_TO_TICKS(2000));
      esp_restart();
    }

    // checkAndUpdate returns false for: fetch fail, up-to-date, or download fail
    // Check if it was "up-to-date" (no need to retry)
    // We can't distinguish easily, so just check version fetch separately
    std::string body;
    if (ServerApi::fetchFirmwareVersionJson(body, log)) {
      // Fetch succeeded — either up-to-date or download failed
      // Either way, no need to retry the fetch loop
      ESP_LOGI(TAG, "[OTA] version fetch OK, done");
      log.appendf("[OTA] version check done\n");
      return false;
    }

    // Fetch failed — retry
    ESP_LOGW(TAG, "[OTA] version fetch FAIL, retrying in %dms...", (int)kFetchRetryDelayMs);
    log.appendf("[OTA] fetch FAIL, retry in %ds\n", (int)(kFetchRetryDelayMs / 1000));
    vTaskDelay(pdMS_TO_TICKS(kFetchRetryDelayMs));
  }

  ESP_LOGE(TAG, "[OTA] version check TIMEOUT after %dms", (int)kFetchTimeoutMs);
  log.appendf("[OTA] version check TIMEOUT (%ds)\n", (int)(kFetchTimeoutMs / 1000));
  return false;
}

// ============================================================
// Sync task entry
// ============================================================
extern "C" void sync_task_entry(void* arg) {
  auto* ctx = reinterpret_cast<AppContext*>(arg);

  extern void register_task_handles(TaskHandle_t, TaskHandle_t, TaskHandle_t);
  register_task_handles(nullptr, xTaskGetCurrentTaskHandle(), nullptr);

  ConnectivityManager cm;

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    DateTime now{};
    if (!RtcPcf8563::instance().getTime(now)) {
      ESP_LOGW(TAG, "RTC read fail");
      continue;
    }
    // DEBUG: skip isSyncMinute check -> always post every 10-min cycle
    ESP_LOGI(TAG, "DEBUG: sync at minute=%d (always-post mode)", now.minute);

    ctx->state.set(AppState::BIT_SYNC_RUNNING);
    LogBuffer log = LogService::createSessionLog();
    log.appendf("[Sync] start\n");

    // --- Step 1: Single connectivity warmup (only place that powers ON) ---
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

    // --- Step 2: Time sync (20s retry window) ---
    syncTimeWithRetry(cm.active(), log);

    // --- Step 3: Send queued measurements & logs ---
    {
      uint32_t start = (uint32_t)xTaskGetTickCount();
      int sent_meas = 0, sent_log = 0;

      while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < cfg::kSyncWindowMs) {
        MeasurementMsg mm{};
        LogMsg lm{};
        bool did = false;

        if (ctx->bus.popMeasurement(mm, cfg::kQueuePopTimeoutMs)) {
          mm.meta.voltage_mv = AdcDrv::readMilliVolts();

          std::string json = JsonPacker::packWaterLevel(mm);
          ESP_LOGI(TAG, "water_level payload: %s", json.c_str());
          log.appendf("[Sync] TX water_level: %s\n", json.c_str());

          bool ok = false;
          if (cm.active() && cm.active()->type() == CommType::Sim4G) {
            ok = cm.active()->sendPayload(ServerApi::waterLevelUrl(), json, log);
          } else {
            ok = ServerApi::sendWaterLevel(json, log);
          }
          log.appendf("[Sync] water_level send=%d\n", (int)ok);
          sent_meas += ok ? 1 : 0;
          did = true;
        }

        if (ctx->bus.popLog(lm, cfg::kQueuePopTimeoutMs)) {
          lm.meta.voltage_mv = AdcDrv::readMilliVolts();
          std::string json = JsonPacker::packLog(lm);

          bool ok = false;
          if (cm.active() && cm.active()->type() == CommType::Sim4G) {
            ok = cm.active()->sendPayload(ServerApi::logUrl(), json, log);
          } else {
            ok = ServerApi::sendLog(json, log);
          }
          log.appendf("[Sync] log send=%d\n", (int)ok);
          sent_log += ok ? 1 : 0;
          did = true;
        }

        if (!did) vTaskDelay(pdMS_TO_TICKS(50));
      }

      ESP_LOGI(TAG, "send done meas=%d log=%d", sent_meas, sent_log);
    }

    // --- Step 4: OTA version check (20s retry window, DCOM only) ---
    otaCheckWithRetry(cm.active(), log);

    // --- Step 5: Power off (single place) ---
    if (cm.active()) {
      cm.active()->powerOff(log);
    }

    ESP_LOGI(TAG, "sync cycle done");
    ctx->state.clear(AppState::BIT_LAST_SYNC_FAIL);
    ctx->state.clear(AppState::BIT_SYNC_RUNNING);
  }
}
