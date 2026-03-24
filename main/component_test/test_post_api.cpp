#include "test_common.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "board/uart_drv.hpp"
#include "board/pins.hpp"
#include "modules/io/io_controller.hpp"
#include "common/config.hpp"

#include "esp_http_client.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

static const char* NAME = "POST_API";
static const char* kWaterLevelUrl = "http://donuoctrieuduong.xyz/dev_test/water_lever.php";

// Default test payload
static const char* kDefaultSerial = "TD_MW_00012";
static constexpr int kDefaultWaterLevel0 = 15230;
static constexpr int kDefaultWaterLevel1 = 15228;
static constexpr int kDefaultWaterLevel2 = 15231;
static constexpr int kDefaultVoltage     = 4850;

// ──────────────────────────────────────────────
// Build JSON payload
// ──────────────────────────────────────────────
static std::string buildPayload() {
  char buf[512];
  std::snprintf(buf, sizeof(buf),
    "{\"water_lever_0\":%d,"
     "\"water_lever_1\":%d,"
     "\"water_lever_2\":%d,"
     "\"date_time\":\"2026-03-23 10:30:00\","
     "\"serial_number\":\"%s\","
     "\"type\":\"water_lever\","
     "\"vol\":%d}",
    kDefaultWaterLevel0, kDefaultWaterLevel1, kDefaultWaterLevel2,
    kDefaultSerial, kDefaultVoltage);
  return std::string(buf);
}

// ──────────────────────────────────────────────
// SIM 4G HTTP POST helpers (AT commands)
// ──────────────────────────────────────────────
static bool simSendAt(const char* cmd, const char* expect, uint32_t timeoutMs) {
  UartDrv::flushSim();
  TEST_INFO(NAME, "AT> %s", cmd);
  if (!UartDrv::writeLineSim(cmd)) return false;

  std::string resp = UartDrv::readLineSim(timeoutMs);
  TEST_INFO(NAME, "  <- %s", resp.c_str());

  if (resp.find(expect) != std::string::npos) return true;
  if (resp.find("ERROR") != std::string::npos) return false;
  return false;
}

static bool simWaitToken(const char* token, uint32_t timeoutMs, std::string* matched = nullptr) {
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    std::string line = UartDrv::readLineSim(500);
    if (line.empty()) continue;
    TEST_INFO(NAME, "  <- %s", line.c_str());
    if (line.find(token) != std::string::npos) {
      if (matched) *matched = line;
      return true;
    }
    if (line.find("ERROR") != std::string::npos) return false;
  }
  return false;
}

static bool parseHttpAction(const std::string& line, int& method, int& status, int& bodyLen) {
  const char* p = std::strstr(line.c_str(), "+HTTPACTION:");
  if (!p) return false;

  if (std::sscanf(p, "+HTTPACTION: %d,%d,%d", &method, &status, &bodyLen) == 3) return true;
  if (std::sscanf(p, "+HTTPACTION:%d,%d,%d", &method, &status, &bodyLen) == 3) return true;
  return false;
}

static bool simConnect() {
  if (!UartDrv::initSimUart()) return false;

  TEST_INFO(NAME, "SIM: Power ON (HIGH->LOW)");
  IoController::instance().setSimPower(false);
  testDelayMs(100);
  IoController::instance().setSimPower(true);
  TEST_INFO(NAME, "SIM: Wait boot 12s...");
  testDelayMs(12000);

  for (int i = 0; i < 5; i++) {
    if (simSendAt("AT", "OK", 1000)) goto at_ok;
    testDelayMs(500);
  }
  return false;

at_ok:
  simSendAt("ATE0", "OK", 1000);
  if (!simSendAt("AT+CGATT=1", "OK", 5000)) return false;
  if (!simSendAt("AT+CGDCONT=1,\"IP\",\"v-internet\"", "OK", 3000)) return false;
  if (!simSendAt("AT+CGAUTH=1,0", "OK", 2000)) return false;
  if (!simSendAt("AT+CGACT=1,1", "OK", 5000)) return false;
  return true;
}

static void simDisconnect() {
  simSendAt("AT+HTTPTERM", "OK", 1000);
  simSendAt("AT+CGACT=0,1", "OK", 3000);
  IoController::instance().setSimPower(true);
}

static bool simHttpPost(const char* url, const std::string& json, std::string& response) {
  char cmd[320];

  TEST_INFO(NAME, "SIM HTTP POST url='%s'", url);
  TEST_INFO(NAME, "SIM HTTP POST payload='%s'", json.c_str());

  simSendAt("AT+HTTPTERM", "OK", 1000);
  if (!simSendAt("AT+HTTPINIT", "OK", 2000)) return false;
  testDelayMs(300);

  // Set URL
  std::snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
  if (!simSendAt(cmd, "OK", 2000)) return false;

  // Set Content-Type
  if (!simSendAt("AT+HTTPPARA=\"CONTENT\",\"application/json\"", "OK", 1000)) return false;

  // Set data length
  std::snprintf(cmd, sizeof(cmd), "AT+HTTPDATA=%d,10000", (int)json.size());
  if (!simSendAt(cmd, "DOWNLOAD", 5000)) return false;

  // Send JSON body
  UartDrv::flushSim();
  TEST_INFO(NAME, "DATA> %s", json.c_str());
  if (UartDrv::writeSim(reinterpret_cast<const uint8_t*>(json.data()),
                        (int)json.size()) != (int)json.size()) {
    TEST_INFO(NAME, "SIM payload write failed");
    simSendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }
  if (!simWaitToken("OK", 5000)) return false;

  // HTTP POST (method 1)
  UartDrv::flushSim();
  TEST_INFO(NAME, "AT> AT+HTTPACTION=1");
  if (!UartDrv::writeLineSim("AT+HTTPACTION=1")) {
    simSendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }

  std::string actionLine;
  if (!simWaitToken("+HTTPACTION:", 30000, &actionLine)) {
    simSendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }

  TEST_INFO(NAME, "SIM HTTPACTION raw='%s'", actionLine.c_str());

  // Parse +HTTPACTION: 1,200,xx
  int method = 0, status = 0, bodyLen = 0;
  if (!parseHttpAction(actionLine, method, status, bodyLen)) {
    TEST_INFO(NAME, "SIM HTTPACTION parse failed");
    simSendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }
  TEST_INFO(NAME, "HTTP status=%d bodyLen=%d", status, bodyLen);

  if (status < 200 || status >= 300) {
    simSendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }

  // Read response body
  if (bodyLen > 0) {
    std::snprintf(cmd, sizeof(cmd), "AT+HTTPREAD=0,%d", bodyLen);
    UartDrv::flushSim();
    TEST_INFO(NAME, "AT> %s", cmd);
    UartDrv::writeLineSim(cmd);

    if (simWaitToken("+HTTPREAD:", 5000)) {
      response.clear();
      uint32_t start = (uint32_t)xTaskGetTickCount();
      while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < 5000) {
        std::string line = UartDrv::readLineSim(500);
        if (line.empty()) continue;
        if (line.find("OK") != std::string::npos) break;
        if (line.find("ERROR") != std::string::npos) break;
        if (!response.empty()) response += '\n';
        response += line;
      }
    }
  }

  TEST_INFO(NAME, "SIM HTTP response body='%s'", response.c_str());

  simSendAt("AT+HTTPTERM", "OK", 1000);
  return true;
}

// ──────────────────────────────────────────────
// DCOM Wi-Fi HTTP POST helpers
// ──────────────────────────────────────────────
static constexpr EventBits_t kConnBit = BIT0;
static EventGroupHandle_t s_evt = nullptr;

static void wifiCb(void*, esp_event_base_t base, int32_t id, void*) {
  if (!s_evt) return;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(s_evt, kConnBit);
  }
}

static bool dcomConnect() {
  return 0;
  TEST_INFO(NAME, "DCOM: Power ON");
  IoController::instance().setDcomPower(false);
  testDelayMs(100);
  IoController::instance().setDcomPower(true);
  testDelayMs(500);

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&init_cfg);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;

  s_evt = xEventGroupCreate();
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiCb, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiCb, nullptr);

  wifi_config_t wifi_cfg{};
  strncpy(reinterpret_cast<char*>(wifi_cfg.sta.ssid),
          cfg::kDcomWifiSsid, sizeof(wifi_cfg.sta.ssid) - 1);
  strncpy(reinterpret_cast<char*>(wifi_cfg.sta.password),
          cfg::kDcomWifiPassword, sizeof(wifi_cfg.sta.password) - 1);
  wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
  esp_wifi_start();

  TEST_INFO(NAME, "DCOM: Connecting to '%s'...", cfg::kDcomWifiSsid);
  xEventGroupClearBits(s_evt, kConnBit);
  esp_wifi_connect();

  EventBits_t bits = xEventGroupWaitBits(s_evt, kConnBit, pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));
  return (bits & kConnBit) != 0;
}

static void dcomDisconnect() {
  esp_wifi_disconnect();
  esp_wifi_stop();
  IoController::instance().setDcomPower(false);
  if (s_evt) {
    vEventGroupDelete(s_evt);
    s_evt = nullptr;
  }
}

static bool dcomHttpPost(const char* url, const std::string& json, std::string& response) {
  esp_http_client_config_t cfg{};
  cfg.url = url;
  cfg.timeout_ms = 8000;

  TEST_INFO(NAME, "DCOM HTTP POST url='%s'", url);
  TEST_INFO(NAME, "DCOM HTTP POST payload='%s'", json.c_str());

  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return false;

  esp_http_client_set_method(c, HTTP_METHOD_POST);
  esp_http_client_set_header(c, "Content-Type", "application/json");
  esp_http_client_set_post_field(c, json.c_str(), (int)json.size());

  esp_err_t err = esp_http_client_perform(c);
  int code = esp_http_client_get_status_code(c);

  response.clear();
  char buf[256];
  int r = 0;
  while ((r = esp_http_client_read(c, buf, sizeof(buf))) > 0) {
    response.append(buf, buf + r);
  }

  if (err != ESP_OK || code < 200 || code >= 300) {
    TEST_INFO(NAME, "DCOM HTTP POST failed err=%s code=%d response='%s'",
              esp_err_to_name(err), code, response.c_str());
    esp_http_client_cleanup(c);
    return false;
  }

  esp_http_client_cleanup(c);
  TEST_INFO(NAME, "DCOM HTTP POST status=%d response='%s'", code, response.c_str());
  return true;
}

// ──────────────────────────────────────────────
// Main test entry: test_post_api
// ──────────────────────────────────────────────
void test_post_api() {
  TEST_START(NAME);

  // Build default JSON payload
  std::string json = buildPayload();
  TEST_INFO(NAME, "Payload: %s", json.c_str());

  std::string response;
  bool posted = false;
  enum { CONN_NONE, CONN_DCOM, CONN_SIM } connType = CONN_NONE;

  // Try DCOM (Wi-Fi) first
  TEST_INFO(NAME, "--- Trying DCOM Wi-Fi ---");
  if (dcomConnect()) {
    TEST_INFO(NAME, "DCOM connected, POST water_level...");
    posted = dcomHttpPost(kWaterLevelUrl, json, response);
    connType = CONN_DCOM;
  }

  // Fallback to SIM 4G
  if (!posted) {
    if (connType == CONN_DCOM) {
      TEST_INFO(NAME, "DCOM POST failed, disconnecting...");
      dcomDisconnect();
    }

    TEST_INFO(NAME, "--- Trying SIM 4G ---");
    if (simConnect()) {
      TEST_INFO(NAME, "SIM connected, POST water_level...");
      posted = simHttpPost(kWaterLevelUrl, json, response);
      connType = CONN_SIM;
    } else {
      TEST_INFO(NAME, "SIM connect failed");
      simDisconnect();
    }
  }

  // Result
  if (posted) {
    TEST_INFO(NAME, "Server response: '%s'", response.c_str());
    TEST_PASS(NAME);
  } else {
    TEST_FAIL(NAME, "POST failed on both DCOM and SIM");
  }

  // Cleanup
  if (connType == CONN_DCOM) dcomDisconnect();
  if (connType == CONN_SIM) simDisconnect();
}
