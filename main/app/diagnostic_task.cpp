#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "modules/rtc/rtc_pcf8563.hpp"
#include "modules/sensor/sensor_manager.hpp"
#include "modules/sensor/laser_sensor.hpp"
#include "modules/io/io_controller.hpp"
#include "services/connectivity/connectivity_manager.hpp"
#include "services/connectivity/dcom_module.hpp"
#include "services/connectivity/sim4g_module.hpp"
#include "services/logging/log_service.hpp"
#include "services/net/server_api.hpp"
#include "services/pack/json_packer.hpp"
#include "services/ota/ota_service.hpp"
#include "middleware/publish_api.hpp"
#include "board/adc_drv.hpp"
#include "common/config.hpp"
#include "common/nvs_store.hpp"
#include "common/time_utils.hpp"

static const char* TAG = "Diagnostic";

static SemaphoreHandle_t s_diag_done = nullptr;

/// Result of the boot connectivity test: which comm module(s) reached the
/// internet. Whichever is OK is kept powered on for the diagnostic window.
struct DiagConnResult {
  bool dcom_ok = false;  // Wi-Fi (DCOM)
  bool sim_ok  = false;  // 4G (SIM)
};

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

#include "board/pins.hpp"

/// Turn on laser sensor via command (UART command kLaserOnCommand, not IO power pin)
static bool turn_on_laser_sensor_by_command(LogBuffer& log) {
#if PINS_UART1_DEVICE == PINS_UART1_DEVICE_LASER
  ESP_LOGI(TAG, "[Diag] turn on laser sensor via command");
  return LaserSensor::instance().turnOnByCommand(log);
#else
  (void)log;
  return true;
#endif
}

static bool measure_and_send_water_level(ICommModule* module, LogBuffer& log) {
  if (!module) return false;

  MeasurementMsg mm{};
  RtcPcf8563::instance().getTime(mm.time);
  mm.meta.voltage_mv = AdcDrv::readMilliVolts();

  SensorManager& sm = SensorManager::instance();
  ISensor* active = nullptr;
  bool ok = false;
  if (sm.ensureReady(active, log)) {
    int out[cfg::kDistanceSamples]{};
    ok = sm.read3(active, out, log);
#if PINS_UART1_DEVICE != PINS_UART1_DEVICE_LASER
    active->finishMeasurement(log);
#endif
    for (int i = 0; i < cfg::kDistanceSamples; ++i) mm.dist_mm[i] = out[i];
  } else {
    ESP_LOGW(TAG, "[Portal] water_level measure FAIL (sensor warmup failed)");
  }
  mm.valid = ok;

  if (!ok) {
#if SENSOR_DEVICE == SENSOR_DEVICE_PRESSURE
    ESP_LOGW(TAG, "[Portal] water_level measure FAIL - pressure build, sending raw anyway");
#else
    ESP_LOGW(TAG, "[Portal] water_level measure FAIL");
    return false;
#endif
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

// ============================================================
// Boot-time firmware version check + OTA
// ============================================================
// Runs in the diagnostic as soon as a comm module is up, so a freshly published
// server image is picked up at boot instead of waiting for the first hourly
// sync. Routed through whichever module connected: DCOM uses esp_https_ota over
// the netif, SIM uses the AT-command HTTP stack (chunked HTTPREAD into the OTA
// partition). On a successful flash the device restarts into the new image,
// which re-runs this diagnostic on the next cold boot. Mirrors
// sync_task's otaCheckWithRetry.
static void diag_ota_check(ICommModule* module, LogBuffer& log) {
  if (!cfg::kOtaEnabled) {
    ESP_LOGI(TAG, "[OTA] disabled (kOtaEnabled=false)");
    log.appendf("[OTA] disabled\n");
    return;
  }
  if (!cfg::kOtaEndpointsConfigured) {
    ESP_LOGI(TAG, "[OTA] skip (endpoints not configured)");
    log.appendf("[OTA] skip (endpoints not configured)\n");
    return;
  }
  if (!module) {
    ESP_LOGI(TAG, "[OTA] skip (no connected comm)");
    log.appendf("[OTA] skip (no connected comm)\n");
    return;
  }

  const char* commName = (module->type() == CommType::Sim4G) ? "SIM4G" : "DCOM";
  ESP_LOGI(TAG, "[OTA] check start (comm=%s, current=%s)", commName, cfg::kCurrentFwVersion);
  log.appendf("[OTA] check start (comm=%s)\n", commName);

  OtaService ota;
  if (ota.checkAndUpdate(module, cfg::kFirmwareVersionUrl, cfg::kFirmwareBinUrl, log)) {
    // Update flashed + boot partition set — reboot into the new image.
    log.appendf("[OTA] update OK, restarting...\n");
    ESP_LOGI(TAG, "[OTA] success, restarting in 2s...");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
  }

  // false = up-to-date or this boot's attempt failed; sync cycle retries later.
  ESP_LOGI(TAG, "[OTA] no update applied");
  log.appendf("[OTA] no update applied\n");
}

static void send_diagnostic_status_log(ICommModule* module, const LogMsg& lm, LogBuffer& log) {
  if (!module) return;

  std::string json = JsonPacker::packLog(lm);
  ESP_LOGI(TAG, "[Diag] POSTing diagnostic status log: %s", json.c_str());
  log.appendf("[Diag] POSTing diagnostic status log: %s\n", json.c_str());

  bool sent = false;
  if (module->type() == CommType::Sim4G) {
    sent = module->sendPayload(ServerApi::logUrl(), json, log);
  } else {
    sent = ServerApi::sendLog(json, log);
  }

  ESP_LOGI(TAG, "[Diag] status log send result: %s", sent ? "SUCCESS" : "FAIL");
  log.appendf("[Diag] status log send result: %s\n", sent ? "SUCCESS" : "FAIL");
}

static void send_firmware_version_info(ICommModule* module, LogBuffer& log) {
  if (!module) return;

  char serial[32]{};
  NvsStore::getDeviceSerial(serial, sizeof(serial));
  if (std::strlen(serial) == 0) {
    std::strcpy(serial, "TD_MW_0007"); // fallback as requested / default
  }

  // Strip "ver" prefix if present
  const char* raw_ver = cfg::kCurrentFwVersion;
  if (std::strncmp(raw_ver, "ver", 3) == 0) {
    raw_ver += 3;
  }

  // Construct JSON: { "device_id": "...", "firmware_version": "..." }
  std::string json = "{";
  json += "\"device_id\":\"";
  json += serial;
  json += "\",\"firmware_version\":\"";
  json += raw_ver;
  json += "\"}";

  ESP_LOGI(TAG, "[FW_API] POSTing fw version: %s", json.c_str());
  log.appendf("[FW_API] POSTing fw version: %s\n", json.c_str());

  bool sent = false;
  if (module->type() == CommType::Sim4G) {
    sent = module->sendPayload(ServerApi::firmwareVersionApiUrl(), json, log);
  } else {
    sent = ServerApi::sendFirmwareVersion(json, log);
  }

  ESP_LOGI(TAG, "[FW_API] send result: %s", sent ? "SUCCESS" : "FAIL");
  log.appendf("[FW_API] send result: %s\n", sent ? "SUCCESS" : "FAIL");
}

/// Run connectivity test: bring up SIM + DCOM in parallel and keep whichever
/// reached the internet powered on. Nothing is powered off here — the diagnostic
/// active window that follows uses the link for water-level sends, time sync and
/// the fw-version/OTA check. Wi-Fi (DCOM) in particular stays up the whole time.
static DiagConnResult test_connectivity() {
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

  return DiagConnResult{ .dcom_ok = dcom_ok, .sim_ok = sim_ok };
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
  DiagConnResult diag = test_connectivity();

  const char* wifi_status = DcomModule::instance().statusDetail();
  const char* sim_status  = Sim4GModule::instance().statusDetail();

  ESP_LOGI(TAG, "[Diagnostic] %s", wifi_status);
  ESP_LOGI(TAG, "[Diagnostic] %s", sim_status);

  LogMsg diag_lm{};
  RtcPcf8563::instance().getTime(diag_lm.time);
  diag_lm.meta.voltage_mv = AdcDrv::readMilliVolts();
  std::snprintf(diag_lm.text, sizeof(diag_lm.text),
                "[Diagnostic] %s | %s",
                wifi_status, sim_status);
  diag_lm.len = static_cast<uint16_t>(std::min(std::strlen(diag_lm.text), sizeof(diag_lm.text) - 1));

  PublishApi::publishLog(diag_lm);

  ESP_LOGI(TAG, "========== BOOT DIAGNOSTIC END ==========");

  // ========================================================
  // Diagnostic active window
  // ========================================================
  // Keep the comm link up and exercise the full data path. Prefer Wi-Fi (DCOM)
  // for the data path, fall back to SIM; the unused module is powered off to save
  // current. Every cfg::kDiagCycleIntervalMs (30s) we send a water-level reading,
  // sync the RTC from the server, and run the fw-version/OTA check.
  //
  // Window length:
  //   - no OTA update -> fixed cfg::kDiagWindowMs (120s)
  //   - OTA update    -> diag_ota_check blocks on the download with its own
  //                      timeout and restarts the device on success, so the fixed
  //                      window only bounds the no-OTA case.
  ICommModule* active = nullptr;
  const char* active_name = "none";
  {
    LogBuffer log = LogService::createSessionLog();
    if (diag.dcom_ok) {
      active = &DcomModule::instance();              // Wi-Fi stays up the whole window
      active_name = "DCOM";
      NvsStore::setLastSuccessComm(CommType::Dcom);
      if (diag.sim_ok) Sim4GModule::instance().powerOff(log);  // unused on this path
    } else if (diag.sim_ok) {
      active = &Sim4GModule::instance();
      active_name = "SIM4G";
      NvsStore::setLastSuccessComm(CommType::Sim4G);
    }
  }

  if (active) {
    ESP_LOGI(TAG, "[Diag] active window %lu ms via %s (cycle every %lu ms)",
             (unsigned long)cfg::kDiagWindowMs, active_name,
             (unsigned long)cfg::kDiagCycleIntervalMs);

    const uint32_t window_start = (uint32_t)xTaskGetTickCount();
    uint32_t last_cycle_tick = 0;
    bool first_cycle = true;
    uint32_t cycle = 0;

    while (pdTICKS_TO_MS((uint32_t)xTaskGetTickCount() - window_start)
           < cfg::kDiagWindowMs) {
      uint32_t now_tick = (uint32_t)xTaskGetTickCount();
      if (first_cycle ||
          pdTICKS_TO_MS(now_tick - last_cycle_tick) >= cfg::kDiagCycleIntervalMs) {
        last_cycle_tick = now_tick;  // pace from cycle start so cadence stays ~30s
        ESP_LOGI(TAG, "[Diag] cycle #%lu via %s", (unsigned long)(++cycle), active_name);
        LogBuffer log = LogService::createSessionLog();

        measure_and_send_water_level(active, log);  // bắn dữ liệu mực nước
        turn_on_laser_sensor_by_command(log);       // bật cảm biến laser bằng command
        fetch_and_sync_time(active, log);           // sync time
        if (first_cycle) {
          send_diagnostic_status_log(active, diag_lm, log);
          send_firmware_version_info(active, log);  // report current fw to dashboard
        }
        diag_ota_check(active, log);                // check fw-version + OTA (may restart)

        first_cycle = false;
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
    }

    LogBuffer log = LogService::createSessionLog();
    active->powerOff(log);
#if PINS_UART1_DEVICE == PINS_UART1_DEVICE_LASER
    LaserSensor::instance().finishMeasurement(log);
#endif
  } else {
    ESP_LOGW(TAG, "[Diag] no comm module reached internet, skipping active window");
  }

  ESP_LOGI(TAG, "========== DIAGNOSTIC WINDOW COMPLETE ==========");
  ESP_LOGI(TAG, "starting main tasks in %lu ms...",
           (unsigned long)cfg::kDiagnosticDelayMs);
  vTaskDelay(pdMS_TO_TICKS(cfg::kDiagnosticDelayMs));

  xSemaphoreGive(s_diag_done);
  vTaskDelete(nullptr);
}

void diagnostic_run_blocking() {
  s_diag_done = xSemaphoreCreateBinary();

  // Larger stack for the diagnostic active window + sensor reads
  xTaskCreate(&diagnostic_task_fn, "diagnostic", 12288, nullptr, 8, nullptr);

  xSemaphoreTake(s_diag_done, portMAX_DELAY);
  vSemaphoreDelete(s_diag_done);
  s_diag_done = nullptr;
}
