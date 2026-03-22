#include "services/portal/boot_portal.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "common/config.hpp"
#include "common/nvs_store.hpp"
#include "common/time_utils.hpp"
#include "board/adc_drv.hpp"
#include "modules/rtc/rtc_pcf8563.hpp"
#include "modules/sensor/sensor_manager.hpp"
#include "services/logging/log_service.hpp"

static const char* TAG = "Portal";

// ============================================================
// Static state
// ============================================================
static httpd_handle_t s_httpd        = nullptr;
static esp_netif_t*   s_ap_netif     = nullptr;
static bool           s_active       = false;
static TickType_t     s_last_request = 0;
static PortalDiagResult s_diag{};

// ============================================================
// JSON helper: extract int value from "key": <number>
// ============================================================
static int json_int(const char* json, const char* key, int def) {
  char pat[64];
  std::snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* p = std::strstr(json, pat);
  if (!p) return def;
  p += std::strlen(pat);
  while (*p == ' ' || *p == '\t') p++;
  return std::atoi(p);
}

// ============================================================
// Status strings
// ============================================================
static const char* wifi_status_str() {
  if (!s_diag.dcom_power) return "WIFI_OFF";
  return s_diag.dcom_ok ? "WIFI_OK" : "WIFI_NO_NETWORK";
}

static const char* sim_status_str() {
  if (!s_diag.sim_power) return "SIM_OFF";
  return s_diag.sim_ok ? "SIM_OK" : "SIM_FAIL";
}

// ============================================================
// HTML page (embedded in flash)
// ============================================================
static const char HTML_PAGE[] = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TRAM DO NUOC</title>
<style>
*{box-sizing:border-box}
body{font-family:sans-serif;max-width:640px;margin:0 auto;padding:16px;background:#f9f9f9}
h2{text-align:center;color:#2c3e50}
.card{background:#fff;border-radius:8px;padding:16px;margin:12px 0;box-shadow:0 1px 4px rgba(0,0,0,.1)}
.row{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid #eee}
.row:last-child{border-bottom:none}
.lbl{font-weight:600;color:#555}
.val{color:#222}
.ok{color:#27ae60}.fail{color:#e74c3c}
input[type=number]{width:120px;padding:6px;border:1px solid #ccc;border-radius:4px}
label{display:block;margin:10px 0}
button{padding:10px 24px;margin:6px 4px;border:none;border-radius:6px;cursor:pointer;font-size:14px}
.btn-reload{background:#3498db;color:#fff}
.btn-save{background:#27ae60;color:#fff}
.btn-reload:hover{background:#2980b9}
.btn-save:hover{background:#219a52}
#msg{margin:10px 0;padding:10px;border-radius:4px;display:none}
</style></head>
<body>
<h2>TRAM DO NUOC</h2>

<div class="card" id="st">
 <div class="row"><span class="lbl">Serial</span><span class="val" id="serial">---</span></div>
 <div class="row"><span class="lbl">Sensor</span><span class="val" id="sensor">---</span></div>
 <div class="row"><span class="lbl">Water Level (raw)</span><span class="val" id="wlr">---</span></div>
 <div class="row"><span class="lbl">Water Level (cal)</span><span class="val" id="wlc">---</span></div>
 <div class="row"><span class="lbl">Voltage (raw)</span><span class="val" id="vr">---</span></div>
 <div class="row"><span class="lbl">Voltage (cal)</span><span class="val" id="vc">---</span></div>
 <div class="row"><span class="lbl">RTC Time</span><span class="val" id="rtc">---</span></div>
 <div class="row"><span class="lbl">Wi-Fi</span><span class="val" id="wifi">---</span></div>
 <div class="row"><span class="lbl">SIM</span><span class="val" id="sim">---</span></div>
 <div class="row"><span class="lbl">Saved Offset</span><span class="val" id="off">---</span></div>
 <div class="row"><span class="lbl">Saved K</span><span class="val" id="kf">---</span></div>
</div>

<button class="btn-reload" onclick="reload()">Reload</button>

<div class="card">
 <h3 style="margin-top:0">Configuration</h3>
 <label>Device Code (digits):<br><input type="number" id="dc" min="0" max="99999" placeholder="e.g. 100"></label>
 <label>Current Water Level (mm):<br><input type="number" id="uwl" placeholder="real-world value"></label>
 <label>Current Voltage (mV):<br><input type="number" id="uv" placeholder="multimeter value"></label>
 <button class="btn-save" onclick="save()">Save</button>
</div>

<div id="msg"></div>

<script>
function c(id){return document.getElementById(id)}
function cls(s){return s.indexOf('OK')>=0?'ok':'fail'}

function reload(){
 fetch('/api/status').then(r=>r.json()).then(d=>{
  c('serial').textContent=d.serial;
  c('sensor').textContent=d.sensor_ok?d.sensor:'FAIL';
  c('wlr').textContent=d.wl_raw+' mm';
  c('wlc').textContent=d.wl_cal+' mm';
  c('vr').textContent=d.vol_raw+' mV';
  c('vc').textContent=d.vol_cal+' mV';
  c('rtc').textContent=d.rtc;
  c('wifi').innerHTML='<span class="'+cls(d.wifi)+'">'+d.wifi+'</span>';
  c('sim').innerHTML='<span class="'+cls(d.sim)+'">'+d.sim+'</span>';
  c('off').textContent=d.offset+' mm';
  c('kf').textContent=d.k_str;
  c('dc').value=d.dev_code;
  c('uwl').value=d.wl_raw;
  c('uv').value=d.vol_raw;
 }).catch(e=>{showMsg('Fetch error: '+e,'#e74c3c')});
}

function save(){
 var body=JSON.stringify({
  device_code:parseInt(c('dc').value)||0,
  user_water_level:parseInt(c('uwl').value)||0,
  user_voltage:parseInt(c('uv').value)||0
 });
 fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:body})
  .then(r=>r.json()).then(d=>{
   showMsg(d.ok?'Saved! offset='+d.offset+' K='+d.k_str:'Error','#27ae60');
   reload();
  }).catch(e=>{showMsg('Error: '+e,'#e74c3c')});
}

function showMsg(t,bg){var m=c('msg');m.textContent=t;m.style.display='block';m.style.background=bg;m.style.color='#fff';setTimeout(()=>{m.style.display='none'},4000)}
reload();
</script>
</body></html>
)rawliteral";

// ============================================================
// HTTP handler: GET / — serve HTML page
// ============================================================
static esp_err_t handle_root(httpd_req_t* req) {
  BootPortal::touch();
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// ============================================================
// HTTP handler: GET /api/status — read sensors, return JSON
// ============================================================
static esp_err_t handle_status(httpd_req_t* req) {
  BootPortal::touch();
  ESP_LOGI(TAG, "GET /api/status");

  // --- On-demand LOCAL reads only (no network fetch, no module re-check) ---
  // Sensor: read current distance
  LogBuffer log = LogService::createSessionLog();
  SensorManager& sm = SensorManager::instance();
  ISensor* sensor = nullptr;
  int wl_raw = 0;
  bool sensor_ok = sm.ensureReady(sensor, log) && sensor->readDistanceMm(wl_raw, log);
  const char* sensor_name = "None";
  if (sensor) {
    sensor_name = (sensor->type() == SensorType::Laser) ? "Laser" : "Ultrasonic";
  }

  // RTC: read local clock
  DateTime now{};
  RtcPcf8563::instance().getTime(now);

  // ADC: read local voltage
  int adc_mv = AdcDrv::readMilliVolts();

  // NVS: load saved calibration
  uint16_t dev_code    = NvsStore::getDeviceCode();
  int32_t  offset      = NvsStore::getWaterLevelOffset();
  uint32_t k_x1000     = NvsStore::getVoltageK();

  char serial[20];
  NvsStore::getDeviceSerial(serial, sizeof(serial));

  // Compute calibrated values
  int wl_cal  = wl_raw - (int)offset;
  int vol_cal = (int)((int64_t)adc_mv * (int64_t)k_x1000 / 1000);

  // K as string "1.234"
  char k_str[16];
  std::snprintf(k_str, sizeof(k_str), "%u.%03u",
                (unsigned)(k_x1000 / 1000), (unsigned)(k_x1000 % 1000));

  // Wi-Fi / SIM status: reuse boot diagnostic result (no re-check)
  // Build JSON
  char json[512];
  std::snprintf(json, sizeof(json),
    "{\"serial\":\"%s\",\"dev_code\":%u,"
    "\"sensor\":\"%s\",\"sensor_ok\":%s,"
    "\"wl_raw\":%d,\"wl_cal\":%d,"
    "\"vol_raw\":%d,\"vol_cal\":%d,"
    "\"rtc\":\"%04d-%02d-%02d %02d:%02d:%02d\","
    "\"wifi\":\"%s\",\"sim\":\"%s\","
    "\"offset\":%d,\"k_str\":\"%s\"}",
    serial, (unsigned)dev_code,
    sensor_name, sensor_ok ? "true" : "false",
    wl_raw, wl_cal,
    adc_mv, vol_cal,
    now.year, now.month, now.day, now.hour, now.minute, now.second,
    wifi_status_str(), sim_status_str(),
    (int)offset, k_str
  );

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json);
  return ESP_OK;
}

// ============================================================
// HTTP handler: POST /api/config — save calibration
// ============================================================
static esp_err_t handle_config(httpd_req_t* req) {
  BootPortal::touch();
  ESP_LOGI(TAG, "POST /api/config");

  // Read request body
  char body[256]{};
  int len = httpd_req_recv(req, body, sizeof(body) - 1);
  if (len <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
    return ESP_FAIL;
  }
  body[len] = '\0';
  ESP_LOGI(TAG, "config body: %s", body);

  int dev_code  = json_int(body, "device_code", -1);
  int user_wl   = json_int(body, "user_water_level", 0);
  int user_vol  = json_int(body, "user_voltage", 0);

  // --- Save device code ---
  if (dev_code >= 0 && dev_code <= 99999) {
    NvsStore::setDeviceCode((uint16_t)dev_code);
    ESP_LOGI(TAG, "device_code -> %d", dev_code);
  }

  // --- Compute & save water-level offset ---
  int32_t offset = NvsStore::getWaterLevelOffset();
  if (user_wl > 0) {
    // Read current sensor value
    LogBuffer log = LogService::createSessionLog();
    SensorManager& sm = SensorManager::instance();
    ISensor* sensor = nullptr;
    int measured = 0;
    if (sm.ensureReady(sensor, log) && sensor->readDistanceMm(measured, log)) {
      offset = (int32_t)(measured - user_wl);
      NvsStore::setWaterLevelOffset(offset);
      ESP_LOGI(TAG, "wl_offset: measured=%d - user=%d = %d", measured, user_wl, (int)offset);
    } else {
      ESP_LOGW(TAG, "sensor read FAIL, cannot compute offset");
    }
  }

  // --- Compute & save voltage K ---
  uint32_t k_x1000 = NvsStore::getVoltageK();
  if (user_vol > 0) {
    int adc_mv = AdcDrv::readMilliVolts();
    if (adc_mv > 0) {
      k_x1000 = (uint32_t)((int64_t)user_vol * 1000 / adc_mv);
      NvsStore::setVoltageK(k_x1000);
      ESP_LOGI(TAG, "vol_k: user=%d / measured=%d = %u (x1000)", user_vol, adc_mv, (unsigned)k_x1000);
    } else {
      ESP_LOGW(TAG, "ADC read 0, cannot compute K");
    }
  }

  // K as string
  char k_str[16];
  std::snprintf(k_str, sizeof(k_str), "%u.%03u",
                (unsigned)(k_x1000 / 1000), (unsigned)(k_x1000 % 1000));

  // Response
  char resp[128];
  std::snprintf(resp, sizeof(resp),
    "{\"ok\":true,\"offset\":%d,\"k_str\":\"%s\"}", (int)offset, k_str);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, resp);
  return ESP_OK;
}

// ============================================================
// Wi-Fi AP setup / teardown
// ============================================================
static bool start_wifi_ap() {
  // Ensure netif + event loop (idempotent)
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "netif init fail: %s", esp_err_to_name(err));
    return false;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "event loop fail: %s", esp_err_to_name(err));
    return false;
  }

  // Create AP netif (only once)
  if (!s_ap_netif) {
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
      ESP_LOGE(TAG, "create AP netif fail");
      return false;
    }
  }

  // Init Wi-Fi driver (idempotent)
  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&init_cfg);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "wifi init fail: %s", esp_err_to_name(err));
    return false;
  }

  // Configure AP mode
  wifi_config_t ap_cfg{};
  std::strncpy(reinterpret_cast<char*>(ap_cfg.ap.ssid),
               cfg::kPortalApSsid,
               sizeof(ap_cfg.ap.ssid) - 1);
  ap_cfg.ap.ssid_len    = (uint8_t)std::strlen(cfg::kPortalApSsid);
  ap_cfg.ap.channel     = 1;
  ap_cfg.ap.authmode    = WIFI_AUTH_OPEN;
  ap_cfg.ap.max_connection = 4;

  err = esp_wifi_set_mode(WIFI_MODE_AP);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "set AP mode fail: %s", esp_err_to_name(err));
    return false;
  }
  err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "set AP config fail: %s", esp_err_to_name(err));
    return false;
  }
  err = esp_wifi_start();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "wifi start fail: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "Wi-Fi AP started: SSID=%s  IP=192.168.4.1", cfg::kPortalApSsid);
  return true;
}

static void stop_wifi_ap() {
  (void)esp_wifi_stop();
  ESP_LOGI(TAG, "Wi-Fi AP stopped");
}

// ============================================================
// HTTP server setup / teardown
// ============================================================
static bool start_http_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.stack_size     = 8192;    // larger stack for sensor reads
  config.max_uri_handlers = 4;

  esp_err_t err = httpd_start(&s_httpd, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "httpd start fail: %s", esp_err_to_name(err));
    return false;
  }

  // Register URI handlers
  static const httpd_uri_t uri_root = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = handle_root,
    .user_ctx = nullptr
  };
  static const httpd_uri_t uri_status = {
    .uri      = "/api/status",
    .method   = HTTP_GET,
    .handler  = handle_status,
    .user_ctx = nullptr
  };
  static const httpd_uri_t uri_config = {
    .uri      = "/api/config",
    .method   = HTTP_POST,
    .handler  = handle_config,
    .user_ctx = nullptr
  };

  httpd_register_uri_handler(s_httpd, &uri_root);
  httpd_register_uri_handler(s_httpd, &uri_status);
  httpd_register_uri_handler(s_httpd, &uri_config);

  ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
  return true;
}

static void stop_http_server() {
  if (s_httpd) {
    httpd_stop(s_httpd);
    s_httpd = nullptr;
  }
  ESP_LOGI(TAG, "HTTP server stopped");
}

// ============================================================
// Public API
// ============================================================
bool BootPortal::start(const PortalDiagResult& diag) {
  s_diag = diag;
  s_last_request = xTaskGetTickCount();
  s_active = false;

  // Suppress noisy httpd warnings (error 113 = EHOSTUNREACH when client
  // disconnects mid-transfer — normal on AP mode, not a real error)
  esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);

  if (!start_wifi_ap()) {
    ESP_LOGE(TAG, "portal start FAIL (AP)");
    return false;
  }
  if (!start_http_server()) {
    stop_wifi_ap();
    ESP_LOGE(TAG, "portal start FAIL (HTTP)");
    return false;
  }

  s_active = true;
  ESP_LOGI(TAG, "=== CONFIG PORTAL ACTIVE ===");
  ESP_LOGI(TAG, "Connect to Wi-Fi: %s", cfg::kPortalApSsid);
  ESP_LOGI(TAG, "Open browser: http://192.168.4.1");
  ESP_LOGI(TAG, "Inactivity timeout: %lu ms", (unsigned long)cfg::kPortalInactivityTimeoutMs);
  return true;
}

void BootPortal::stop() {
  if (!s_active) return;
  stop_http_server();
  stop_wifi_ap();
  s_active = false;
  ESP_LOGI(TAG, "=== CONFIG PORTAL STOPPED ===");
}

uint32_t BootPortal::msSinceLastRequest() {
  if (s_last_request == 0) return UINT32_MAX;
  return pdTICKS_TO_MS(xTaskGetTickCount() - s_last_request);
}

bool BootPortal::isActive() {
  return s_active;
}

void BootPortal::touch() {
  s_last_request = xTaskGetTickCount();
}
