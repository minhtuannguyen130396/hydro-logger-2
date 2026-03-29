#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "modules/rtc/rtc_pcf8563.hpp"
#include "modules/sensor/sensor_manager.hpp"
#include "modules/io/io_controller.hpp"
#include "services/connectivity/connectivity_manager.hpp"
#include "services/connectivity/dcom_module.hpp"
#include "services/connectivity/sim4g_module.hpp"
#include "services/logging/log_service.hpp"
#include "services/net/server_api.hpp"
#include "services/pack/json_packer.hpp"
#include "services/portal/boot_portal.hpp"
#include "board/adc_drv.hpp"
#include "common/config.hpp"
#include "common/nvs_store.hpp"
#include "common/time_utils.hpp"

static const char* TAG = "Diagnostic";

static SemaphoreHandle_t s_diag_done = nullptr;

// ============================================================
// Individual diagnostic tests
// ============================================================

static void test_rtc() {
  DateTime now{};
  bool ok = RtcPcf8563::instance().getTime(now);
  if (ok) {
    ESP_LOGI(TAG, "[RTC] OK -> %04d-%02d-%02d %02d:%02d:%02d",
             now.year, now.month, now.day, now.hour, now.minute, now.second);
  } else {
    ESP_LOGW(TAG, "[RTC] FAIL (I2C or PCF8563 not connected)");
  }
}

static void test_adc() {
  int mv = AdcDrv::readMilliVolts();
  ESP_LOGI(TAG, "[ADC] voltage = %d mV", mv);
}

static void test_sensor() {
  LogBuffer log = LogService::createSessionLog();
  SensorManager& sm = SensorManager::instance();
  ISensor* active = nullptr;

  if (sm.ensureReady(active, log)) {
    int mm = 0;
    if (active->readDistanceMm(mm, log)) {
      ESP_LOGI(TAG, "[Sensor] OK type=%s dist=%d mm",
               active->type() == SensorType::Laser ? "Laser" : "Ultrasonic", mm);
    } else {
      ESP_LOGW(TAG, "[Sensor] read FAIL (warmup OK but read failed)");
    }
  } else {
    ESP_LOGW(TAG, "[Sensor] FAIL (warmup failed or no sensor selected)");
  }
}

// ============================================================
// Connectivity test (parallel SIM + DCOM)
// ============================================================

static constexpr uint32_t kConnDiagTimeoutMs = 60000;

#define CONN_BIT_DCOM  (1 << 0)
#define CONN_BIT_SIM   (1 << 1)
#define CONN_BIT_ALL   (CONN_BIT_DCOM | CONN_BIT_SIM)

struct ConnTaskParam {
  ICommModule* module;
  EventGroupHandle_t event;
  EventBits_t bit;
  bool power_ok;
  bool internet_ok;
};

static void conn_module_task(void* arg) {
  auto* p = static_cast<ConnTaskParam*>(arg);
  LogBuffer log = LogService::createSessionLog();

  p->power_ok = p->module->powerOn(log);
  if (!p->power_ok) {
    vTaskDelete(nullptr);
    return;
  }

  p->internet_ok = p->module->checkInternet(kConnDiagTimeoutMs, log);
  if (!p->internet_ok) {
    p->module->powerOff(log);
  }

  if (p->internet_ok) {
    xEventGroupSetBits(p->event, p->bit);
  }
  vTaskDelete(nullptr);
}

/// Fetch server time and sync RTC. Returns true on success.
static bool fetch_and_sync_time(ICommModule* module, LogBuffer& log) {
  if (!module) return false;

  std::string timeStr;
  bool fetched = false;

  if (module->type() == CommType::Sim4G) {
    fetched = static_cast<Sim4GModule*>(module)->httpGet(
        ServerApi::timeUrl(), timeStr, log);
  } else {
    fetched = ServerApi::fetchServerTime(timeStr, log);
  }

  if (!fetched) {
    ESP_LOGW(TAG, "[TimeSync] fetch FAIL");
    return false;
  }

  DateTime serverTime{};
  if (!timeu::parseServerTime(timeStr.c_str(), serverTime)) {
    ESP_LOGW(TAG, "[TimeSync] parse FAIL: '%s'", timeStr.c_str());
    return false;
  }

  DateTime rtcNow{};
  RtcPcf8563::instance().getTime(rtcNow);
  int64_t delta = timeu::deltaSeconds(serverTime, rtcNow);
  ESP_LOGI(TAG, "[TimeSync] delta=%llds", (long long)delta);

  if (delta > 60) {
    RtcPcf8563::instance().setTime(serverTime);
    ESP_LOGI(TAG, "[TimeSync] RTC synced -> %04d-%02d-%02d %02d:%02d:%02d",
             serverTime.year, serverTime.month, serverTime.day,
             serverTime.hour, serverTime.minute, serverTime.second);
  } else {
    ESP_LOGI(TAG, "[TimeSync] RTC OK (delta <= 60s)");
  }
  return true;
}

static bool measure_and_send_water_level(ICommModule* module, LogBuffer& log) {
  if (!module) return false;

  MeasurementMsg mm{};
  RtcPcf8563::instance().getTime(mm.time);
  mm.meta.voltage_mv = AdcDrv::readMilliVolts();

  SensorManager& sm = SensorManager::instance();
  ISensor* active = nullptr;
  if (!sm.ensureReady(active, log)) {
    ESP_LOGW(TAG, "[Portal] water_level measure FAIL (sensor warmup failed)");
    return false;
  }

  int out[cfg::kDistanceSamples]{};
  bool ok = sm.read3(active, out, log);
  active->finishMeasurement(log);
  for (int i = 0; i < cfg::kDistanceSamples; ++i) mm.dist_mm[i] = out[i];
  mm.valid = ok;

  if (!ok) {
    ESP_LOGW(TAG, "[Portal] water_level measure FAIL (sensor read failed)");
    return false;
  }

  std::string json = JsonPacker::packWaterLevel(mm);
  ESP_LOGI(TAG, "[Portal] TX water_level: %s", json.c_str());

  bool sent = false;
  if (module->type() == CommType::Sim4G) {
    sent = module->sendPayload(ServerApi::waterLevelUrl(), json, log);
  } else {
    sent = ServerApi::sendWaterLevel(json, log);
  }

  ESP_LOGI(TAG, "[Portal] water_level send=%d", (int)sent);
  return sent;
}

/// Run connectivity test, populate PortalDiagResult, keep SIM alive if OK.
static PortalDiagResult test_connectivity() {
  ESP_LOGI(TAG, "[Conn] testing SIM + DCOM in parallel (timeout %lu ms)...",
           (unsigned long)kConnDiagTimeoutMs);

  EventGroupHandle_t eg = xEventGroupCreate();

  static ConnTaskParam dcom_param;
  dcom_param = {&DcomModule::instance(), eg, CONN_BIT_DCOM, false, false};

  static ConnTaskParam sim_param;
  sim_param = {&Sim4GModule::instance(), eg, CONN_BIT_SIM, false, false};

  xTaskCreate(&conn_module_task, "diag_dcom", 4096, &dcom_param, 7, nullptr);
  xTaskCreate(&conn_module_task, "diag_sim",  4096, &sim_param,  7, nullptr);

  // Wait until ANY module succeeds
  EventBits_t bits = xEventGroupWaitBits(
      eg, CONN_BIT_ALL, pdFALSE, pdFALSE, pdMS_TO_TICKS(kConnDiagTimeoutMs));

  // Grace period for the other module
  if ((bits & CONN_BIT_ALL) != 0 && (bits & CONN_BIT_ALL) != CONN_BIT_ALL) {
    ESP_LOGI(TAG, "[Conn] one ready, waiting 3s grace ...");
    bits |= xEventGroupWaitBits(
        eg, CONN_BIT_ALL, pdFALSE, pdTRUE, pdMS_TO_TICKS(3000));
  }

  vEventGroupDelete(eg);

  bool dcom_ok = (bits & CONN_BIT_DCOM) != 0;
  bool sim_ok  = (bits & CONN_BIT_SIM) != 0;

  ESP_LOGI(TAG, "[Conn] result: DCOM=%s SIM=%s",
           dcom_ok ? "OK" : "FAIL", sim_ok ? "OK" : "FAIL");

  // Power off DCOM now (we need Wi-Fi for portal AP later)
  if (dcom_ok) {
    LogBuffer log = LogService::createSessionLog();
    DcomModule::instance().powerOff(log);
  }

  // Keep SIM alive if it succeeded — used for time fetch during portal
  // (SIM UART doesn't conflict with Wi-Fi AP)

  return PortalDiagResult{
    .dcom_ok    = dcom_ok,
    .sim_ok     = sim_ok,
    .dcom_power = dcom_param.power_ok,
    .sim_power  = sim_param.power_ok,
  };
}

// ============================================================
// Main diagnostic task
// ============================================================

static void diagnostic_task_fn(void* arg) {
  ESP_LOGI(TAG, "========== BOOT DIAGNOSTIC START ==========");

  // Load and log persisted calibration
  {
    char serial[20];
    NvsStore::getDeviceSerial(serial, sizeof(serial));
    int32_t offset  = NvsStore::getWaterLevelOffset();
    uint32_t k      = NvsStore::getVoltageK();
    ESP_LOGI(TAG, "[NVS] serial=%s offset=%d k=%u(x1000)", serial, (int)offset, (unsigned)k);
  }

  test_rtc();
  test_adc();
  test_sensor();
  PortalDiagResult diag = test_connectivity();

  ESP_LOGI(TAG, "========== BOOT DIAGNOSTIC END ==========");

  // ========================================================
  // Config portal phase
  // ========================================================
  if (!BootPortal::start(diag)) {
    ESP_LOGW(TAG, "portal start FAIL, skipping portal phase");
  }

  // Portal + repeated time fetch loop
  // - Portal stays alive while HTTP requests arrive within kPortalInactivityTimeoutMs
  // - Time fetch runs every kDiagTimeFetchIntervalMs for kDiagTimeFetchWindowMs total
  ICommModule* time_module = diag.sim_ok
      ? static_cast<ICommModule*>(&Sim4GModule::instance())
      : nullptr;  // only SIM can be used; Wi-Fi is in AP mode
  const char* time_mod_name = time_module ? "SIM4G" : "none";

  const uint32_t portal_start_tick = (uint32_t)xTaskGetTickCount();
  uint32_t last_fetch_tick = 0;
  bool boot_water_level_sent = false;

  ESP_LOGI(TAG, "[Portal] entering portal loop (time via %s)", time_mod_name);

  while (BootPortal::isActive()) {
    uint32_t now_tick = (uint32_t)xTaskGetTickCount();

    // Check portal inactivity timeout
    if (BootPortal::msSinceLastRequest() >= cfg::kPortalInactivityTimeoutMs) {
      ESP_LOGI(TAG, "[Portal] inactivity timeout (%lu ms), stopping",
               (unsigned long)cfg::kPortalInactivityTimeoutMs);
      break;
    }

    // Repeated time fetch (within the time-fetch window)
    uint32_t elapsed = pdTICKS_TO_MS(now_tick - portal_start_tick);
    if (time_module && elapsed < cfg::kDiagTimeFetchWindowMs) {
      uint32_t since_fetch = pdTICKS_TO_MS(now_tick - last_fetch_tick);
      if (last_fetch_tick == 0 || since_fetch >= cfg::kDiagTimeFetchIntervalMs) {
        ESP_LOGI(TAG, "[Portal] time fetch #%lu via %s",
                 (unsigned long)(elapsed / cfg::kDiagTimeFetchIntervalMs + 1), time_mod_name);
        LogBuffer log = LogService::createSessionLog();
        bool synced = fetch_and_sync_time(time_module, log);
        if (synced && !boot_water_level_sent) {
          boot_water_level_sent = measure_and_send_water_level(time_module, log);
        }
        last_fetch_tick = (uint32_t)xTaskGetTickCount();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  BootPortal::stop();

  // Power off SIM if still active
  if (diag.sim_ok) {
    LogBuffer log = LogService::createSessionLog();
    Sim4GModule::instance().powerOff(log);
  }

  ESP_LOGI(TAG, "========== PORTAL PHASE COMPLETE ==========");
  ESP_LOGI(TAG, "starting main tasks in %lu ms...",
           (unsigned long)cfg::kDiagnosticDelayMs);
  vTaskDelay(pdMS_TO_TICKS(cfg::kDiagnosticDelayMs));

  xSemaphoreGive(s_diag_done);
  vTaskDelete(nullptr);
}

void diagnostic_run_blocking() {
  s_diag_done = xSemaphoreCreateBinary();

  // Larger stack for portal + sensor reads
  xTaskCreate(&diagnostic_task_fn, "diagnostic", 12288, nullptr, 8, nullptr);

  xSemaphoreTake(s_diag_done, portMAX_DELAY);
  vSemaphoreDelete(s_diag_done);
  s_diag_done = nullptr;
}
