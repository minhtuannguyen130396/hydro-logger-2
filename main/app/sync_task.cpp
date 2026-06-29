#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app/app_state.hpp"
#include "app/app_context.hpp"
#include "app/failover_task.hpp"
#include "modules/rtc/rtc_pcf8563.hpp"
#include "board/pins.hpp"
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
static constexpr uint32_t kMeasureFlushWaitMs = 45000;

template <typename SendFn>
static bool sendWithRetries(LogBuffer& log,
                            const char* tag,
                            SendFn&& sendFn) {
  for (int attempt = 1; attempt <= cfg::kSyncSendRetries; ++attempt) {
    log.appendf("[Sync] %s send attempt %d/%d\n", tag, attempt, cfg::kSyncSendRetries);
    if (sendFn()) return true;
  }
  return false;
}

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
// OTA version check + update (works over both SIM4G and DCOM)
// ============================================================
// OtaService routes the version fetch and the binary download through the
// active comm module: SIM uses the AT-command HTTP stack (chunked HTTPREAD into
// the OTA partition), DCOM uses esp_https_ota. The download is not wrapped in a
// short retry window here — OtaService retries internally (cfg::kOtaMaxAttempts)
// and a SIM download alone can take 1.5-2.5 min, so the modem must stay powered
// until this returns (power-off happens later in the sync cycle).
static bool otaCheckWithRetry(ICommModule* active, LogBuffer& log) {
  // Master switch — OTA temporarily disabled (see cfg::kOtaEnabled).
  if (!cfg::kOtaEnabled) {
    ESP_LOGI(TAG, "[OTA] disabled (kOtaEnabled=false)");
    log.appendf("[OTA] disabled\n");
    return false;
  }

  if (!active) {
    ESP_LOGI(TAG, "[OTA] skip (no active comm)");
    log.appendf("[OTA] skip (no active comm)\n");
    return false;
  }

  // OTA endpoints are still the compile-time placeholder (real link provided
  // later). Skip the network round-trip until they are configured.
  if (!cfg::kOtaEndpointsConfigured) {
    ESP_LOGI(TAG, "[OTA] skip (endpoints not configured)");
    log.appendf("[OTA] skip (endpoints not configured)\n");
    return false;
  }

  const char* commName = (active->type() == CommType::Sim4G) ? "SIM4G" : "DCOM";
  ESP_LOGI(TAG, "[OTA] check start (comm=%s)", commName);
  log.appendf("[OTA] check start (comm=%s)\n", commName);

  OtaService ota;
  if (ota.checkAndUpdate(active, cfg::kFirmwareVersionUrl, cfg::kFirmwareBinUrl, log)) {
    // Update flashed + boot partition set — reboot into the new image.
    log.appendf("[OTA] update OK, restarting...\n");
    ESP_LOGI(TAG, "OTA success, restarting in 2s...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
  }

  // false = up-to-date or this cycle's attempt failed; next sync cycle retries.
  ESP_LOGI(TAG, "[OTA] no update applied");
  log.appendf("[OTA] no update applied\n");
  return false;
}

// ============================================================
// Sync task entry
// ============================================================
extern "C" void sync_task_entry(void* arg) {
  auto* ctx = reinterpret_cast<AppContext*>(arg);

  ConnectivityManager cm;

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    DateTime now{};
    if (!RtcPcf8563::instance().getTime(now)) {
      ESP_LOGW(TAG, "RTC read fail");
      continue;
    }

    ctx->state.set(AppState::BIT_SYNC_RUNNING);
    LogBuffer log = LogService::createSessionLog();
    log.appendf("[Sync] start\n");

    // --- Step 1: Single connectivity warmup (only place that powers ON) ---
    bool conn_ok = cm.warmup(log);
    if (!conn_ok) {
      const CommType preferred = NvsStore::getLastSuccessComm(CommType::Sim4G);
      enqueue_failover_request(ctx, preferred, FailoverReason::SyncWarmupFail);
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

    // --- Step 3: Early OTA version check (lightweight, JSON only) ---
    // Check if an OTA update is available BEFORE spending time on data sending.
    // If an update is pending, skip measure-wait and data drain to prioritize
    // the download — a successful OTA restarts the device anyway, so unsent
    // data will be picked up from the NVS-mirrored queues on the next boot.
    bool ota_update_pending = false;
    {
      OtaService ota;
      if (cfg::kOtaEnabled && cfg::kOtaEndpointsConfigured && cm.active()) {
        const char* commName = (cm.active()->type() == CommType::Sim4G) ? "SIM4G" : "DCOM";
        ESP_LOGI(TAG, "[OTA] early version check (comm=%s)", commName);
        log.appendf("[Sync] OTA early version check (comm=%s)\n", commName);
        ota_update_pending = ota.checkVersionAvailable(
            cm.active(), cfg::kFirmwareVersionUrl, log);
        if (ota_update_pending) {
          ESP_LOGI(TAG, "[OTA] update available, prioritizing download");
          log.appendf("[Sync] OTA update available -> skip data send, prioritize download\n");
        }
      }
    }

    if (ota_update_pending) {
      // --- OTA priority path: skip measure wait & data sending ---
      ctx->state.set(AppState::BIT_OTA_RUNNING);
      log.appendf("[Sync] OTA priority mode active\n");

      otaCheckWithRetry(cm.active(), log);

      ctx->state.clear(AppState::BIT_OTA_RUNNING);
    } else {
      // --- Normal path: wait for measure, then send data ---

      // Wait until the concurrent measurement finishes publishing so the :00
      // sample is included in this sync batch instead of slipping to the next hour.
      {
        uint32_t wait_start = (uint32_t)xTaskGetTickCount();
        while ((ctx->state.get() & AppState::BIT_MEASURE_RUNNING) &&
               pdTICKS_TO_MS(xTaskGetTickCount() - wait_start) < kMeasureFlushWaitMs) {
          vTaskDelay(pdMS_TO_TICKS(200));
        }
        if (ctx->state.get() & AppState::BIT_MEASURE_RUNNING) {
          log.appendf("[Sync] wait measure timeout after %ums\n", (unsigned)kMeasureFlushWaitMs);
        }
      }

      // --- Step 4: Send queued measurements & logs ---
      {
        uint32_t start = (uint32_t)xTaskGetTickCount();
        int sent_meas = 0, sent_log = 0;
        log.appendf("[Sync] queue start meas=%u log=%u\n",
                    (unsigned)ctx->bus.measureDepth(),
                    (unsigned)ctx->bus.logDepth());

        auto withinSyncWindow = [&start]() {
          return pdTICKS_TO_MS(xTaskGetTickCount() - start) < cfg::kSyncWindowMs;
        };

        // Drain measurements first so the hourly sync does not spend the whole
        // window on logs and leave half of the water level readings unsent.
        while (withinSyncWindow()) {
          MeasurementMsg mm{};
          if (!ctx->bus.peekMeasurement(mm, cfg::kQueuePopTimeoutMs)) break;

          if (!mm.valid) {
#if SENSOR_DEVICE == SENSOR_DEVICE_PRESSURE
            // Pressure build: a failed read (raw=0) is still reported so the
            // server records the data point instead of a gap. The raw value
            // (0) is sent as-is via water_lever_*.
            log.appendf("[Sync] send raw on read-fail d=[%d,%d,%d]\n",
                        mm.dist_mm[0], mm.dist_mm[1], mm.dist_mm[2]);
#else
            log.appendf("[Sync] skip invalid measurement d=[%d,%d,%d]\n",
                        mm.dist_mm[0], mm.dist_mm[1], mm.dist_mm[2]);
            ctx->bus.ackMeasurement();
            continue;
#endif
          }
          mm.meta.voltage_mv = AdcDrv::readMilliVolts();

          std::string json = JsonPacker::packWaterLevel(mm);
          ESP_LOGI(TAG, "water_level payload: %s", json.c_str());
          log.appendf("[Sync] TX water_level: %s\n", json.c_str());

          bool ok = sendWithRetries(log, "water_level", [&]() {
            if (cm.active() && cm.active()->type() == CommType::Sim4G) {
              return cm.active()->sendPayload(ServerApi::waterLevelUrl(), json, log);
            }
            return ServerApi::sendWaterLevel(json, log);
          });
          log.appendf("[Sync] water_level send=%d\n", (int)ok);
          sent_meas += ok ? 1 : 0;
          if (ok) {
            ctx->bus.ackMeasurement();
          } else {
            if (cm.active()) {
              enqueue_failover_request(ctx, cm.active()->type(), FailoverReason::SyncSendFail);
            }
            log.appendf("[Sync] water_level send FAIL after %d attempts -> keep item in queue, stop drain\n",
                        cfg::kSyncSendRetries);
            ctx->state.set(AppState::BIT_LAST_SYNC_FAIL);
            break;
          }
        }

        while (withinSyncWindow()) {
          LogMsg lm{};
          if (!ctx->bus.peekLog(lm, cfg::kQueuePopTimeoutMs)) break;

          lm.meta.voltage_mv = AdcDrv::readMilliVolts();
          std::string json = JsonPacker::packLog(lm);

          bool ok = sendWithRetries(log, "log", [&]() {
            if (cm.active() && cm.active()->type() == CommType::Sim4G) {
              return cm.active()->sendPayload(ServerApi::logUrl(), json, log);
            }
            return ServerApi::sendLog(json, log);
          });
          log.appendf("[Sync] log send=%d\n", (int)ok);
          sent_log += ok ? 1 : 0;
          if (ok) {
            ctx->bus.ackLog();
          } else {
            if (cm.active()) {
              enqueue_failover_request(ctx, cm.active()->type(), FailoverReason::SyncSendFail);
            }
            log.appendf("[Sync] log send FAIL after %d attempts -> keep item in queue, stop drain\n",
                        cfg::kSyncSendRetries);
            ctx->state.set(AppState::BIT_LAST_SYNC_FAIL);
            break;
          }
        }

        const size_t remain_meas = ctx->bus.measureDepth();
        const size_t remain_log = ctx->bus.logDepth();
        log.appendf("[Sync] queue end meas=%u log=%u\n",
                    (unsigned)remain_meas,
                    (unsigned)remain_log);
        ESP_LOGI(TAG, "send done meas=%d log=%d remain_meas=%u remain_log=%u",
                 sent_meas, sent_log,
                 (unsigned)remain_meas,
                 (unsigned)remain_log);
      }

      // --- Step 5: OTA version check (normal path, no early-detect) ---
      otaCheckWithRetry(cm.active(), log);
    }

    // --- Step 6: Power off (single place) ---
    if (cm.active()) {
      cm.active()->powerOff(log);
    }

    ESP_LOGI(TAG, "sync cycle done");
    ctx->state.clear(AppState::BIT_LAST_SYNC_FAIL);
    ctx->state.clear(AppState::BIT_SYNC_RUNNING);
  }
}
