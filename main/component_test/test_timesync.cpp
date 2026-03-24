#include "test_common.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <ctime>
#include <cstdlib>

#include "board/uart_drv.hpp"
#include "board/pins.hpp"
#include "modules/io/io_controller.hpp"
#include "modules/rtc/rtc_pcf8563.hpp"
#include "common/config.hpp"

#include "esp_http_client.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

static const char* NAME = "TIMESYNC";
static const char* kTimeUrl = "http://donuoctrieuduong.xyz/dev_test/get_time.php";

// ──────────────────────────────────────────────
// Time utilities (self-contained for test program)
// ──────────────────────────────────────────────
struct TestDateTime {
  int year, month, day, hour, minute, second;
};

static bool parseTimeStr(const char* str, TestDateTime& out) {
  return std::sscanf(str, "%d:%d:%d_%d:%d:%d",
                     &out.hour, &out.minute, &out.second,
                     &out.day, &out.month, &out.year) == 6;
}

static int64_t toEpoch(const TestDateTime& dt) {
  struct tm t{};
  t.tm_year = dt.year - 1900;
  t.tm_mon  = dt.month - 1;
  t.tm_mday = dt.day;
  t.tm_hour = dt.hour;
  t.tm_min  = dt.minute;
  t.tm_sec  = dt.second;
  t.tm_isdst = 0;
  return static_cast<int64_t>(mktime(&t));
}

static int64_t absDelta(const TestDateTime& a, const TestDateTime& b) {
  int64_t diff = toEpoch(a) - toEpoch(b);
  return diff < 0 ? -diff : diff;
}

// ──────────────────────────────────────────────
// SIM 4G HTTP GET helper (AT commands)
// ──────────────────────────────────────────────
static bool simSendAt(const char* cmd, const char* expect, uint32_t timeoutMs) {
  UartDrv::flushSim();
  TEST_INFO(NAME, "AT> %s", cmd);
  if (!UartDrv::writeLineSim(cmd)) return false;

  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    std::string line = UartDrv::readLineSim(200);
    if (line.empty()) continue;
    TEST_INFO(NAME, "  < %s", line.c_str());
    if (line.find(expect) != std::string::npos) return true;
    if (line.find("ERROR") != std::string::npos) return false;
  }
  return false;
}

static bool simWaitToken(const char* expect, uint32_t timeoutMs, std::string* matched = nullptr) {
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    std::string line = UartDrv::readLineSim(200);
    if (line.empty()) continue;
    TEST_INFO(NAME, "  < %s", line.c_str());
    if (line.find(expect) != std::string::npos) {
      if (matched) *matched = line;
      return true;
    }
    if (line.find("ERROR") != std::string::npos) return false;
  }
  return false;
}

static bool simHttpGet(const char* url, std::string& body) {
  char cmd[320];

  simSendAt("AT+HTTPTERM", "OK", 1000);
  if (!simSendAt("AT+HTTPINIT", "OK", 2000)) return false;
  if (!simSendAt("AT+HTTPPARA=\"CID\",1", "OK", 1000)) return false;

  std::snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
  if (!simSendAt(cmd, "OK", 2000)) return false;

  // HTTP GET (method 0)
  UartDrv::flushSim();
  TEST_INFO(NAME, "AT> AT+HTTPACTION=0");
  UartDrv::writeLineSim("AT+HTTPACTION=0");

  if (!simWaitToken("OK", 2000)) {
    simSendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }

  std::string actionLine;
  if (!simWaitToken("+HTTPACTION:", 30000, &actionLine)) {
    simSendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }

  // Parse +HTTPACTION: 0,200,19
  int method = 0, status = 0, bodyLen = 0;
  std::sscanf(actionLine.c_str(), "+HTTPACTION: %d,%d,%d", &method, &status, &bodyLen);
  TEST_INFO(NAME, "HTTP status=%d bodyLen=%d", status, bodyLen);

  if (status < 200 || status >= 300 || bodyLen <= 0) {
    simSendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }

  // Read response body
  std::snprintf(cmd, sizeof(cmd), "AT+HTTPREAD=0,%d", bodyLen);
  UartDrv::flushSim();
  TEST_INFO(NAME, "AT> %s", cmd);
  UartDrv::writeLineSim(cmd);

  if (!simWaitToken("+HTTPREAD:", 5000)) {
    simSendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }

  // Read body lines until OK
  body.clear();
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < 5000) {
    std::string line = UartDrv::readLineSim(500);
    if (line.empty()) continue;
    if (line.find("OK") != std::string::npos) break;
    if (line.find("ERROR") != std::string::npos) break;
    if (!body.empty()) body += '\n';
    body += line;
  }

  simSendAt("AT+HTTPTERM", "OK", 1000);
  return !body.empty();
}

static bool simConnect() {
  if (!UartDrv::initSimUart()) return false;

  // Power on
  TEST_INFO(NAME, "SIM: Power ON (HIGH->LOW)");
  IoController::instance().setSimPower(true);
  testDelayMs(100);
  IoController::instance().setSimPower(false);
  TEST_INFO(NAME, "SIM: Wait boot 12000ms...");
  testDelayMs(12000);

  // AT handshake
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

// ──────────────────────────────────────────────
// DCOM Wi-Fi HTTP GET helper
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

static bool dcomHttpGet(const char* url, std::string& body) {
  esp_http_client_config_t cfg{};
  cfg.url = url;
  cfg.timeout_ms = 8000;

  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return false;

  esp_http_client_set_method(c, HTTP_METHOD_GET);
  esp_err_t err = esp_http_client_perform(c);
  int code = esp_http_client_get_status_code(c);

  if (err != ESP_OK || code < 200 || code >= 300) {
    TEST_INFO(NAME, "HTTP GET failed err=%s code=%d", esp_err_to_name(err), code);
    esp_http_client_cleanup(c);
    return false;
  }

  body.clear();
  char buf[256];
  int r = 0;
  while ((r = esp_http_client_read(c, buf, sizeof(buf))) > 0) {
    body.append(buf, buf + r);
  }
  esp_http_client_cleanup(c);
  return !body.empty();
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

// ──────────────────────────────────────────────
// Main test function
// ──────────────────────────────────────────────
void test_timesync() {
  TEST_START(NAME);

  // Step 1: Read current RTC time
  RtcPcf8563& rtc = RtcPcf8563::instance();
  rtc.init();

  TestDateTime rtcTime{};
  {
    DateTime now{};
    if (!rtc.getTime(now)) {
      TEST_FAIL(NAME, "RTC read failed");
      return;
    }
    rtcTime = {now.year, now.month, now.day, now.hour, now.minute, now.second};
    TEST_INFO(NAME, "RTC time: %04d-%02d-%02d %02d:%02d:%02d",
              rtcTime.year, rtcTime.month, rtcTime.day,
              rtcTime.hour, rtcTime.minute, rtcTime.second);
  }

  // Step 2: Try DCOM first, fallback to SIM
  std::string body;
  bool fetched = false;
  enum { CONN_NONE, CONN_DCOM, CONN_SIM } connType = CONN_NONE;

  TEST_INFO(NAME, "--- Trying DCOM Wi-Fi ---");
  if (dcomConnect()) {
    TEST_INFO(NAME, "DCOM connected, fetching time...");
    fetched = dcomHttpGet(kTimeUrl, body);
    connType = CONN_DCOM;
  }

  if (!fetched) {
    if (connType == CONN_DCOM) {
      TEST_INFO(NAME, "DCOM HTTP failed, disconnecting...");
      dcomDisconnect();
    }

    TEST_INFO(NAME, "--- Trying SIM 4G ---");
    if (simConnect()) {
      TEST_INFO(NAME, "SIM connected, fetching time...");
      fetched = simHttpGet(kTimeUrl, body);
      connType = CONN_SIM;
    } else {
      TEST_INFO(NAME, "SIM connect failed");
      simDisconnect();
    }
  }

  if (!fetched) {
    if (connType == CONN_DCOM) dcomDisconnect();
    if (connType == CONN_SIM) simDisconnect();
    TEST_FAIL(NAME, "Could not fetch time from server (both DCOM and SIM failed)");
    return;
  }

  TEST_INFO(NAME, "Server response: '%s'", body.c_str());

  // Step 3: Parse server time
  TestDateTime serverTime{};
  if (!parseTimeStr(body.c_str(), serverTime)) {
    TEST_INFO(NAME, "Parse failed for: '%s'", body.c_str());
    if (connType == CONN_DCOM) dcomDisconnect();
    if (connType == CONN_SIM) simDisconnect();
    TEST_FAIL(NAME, "Invalid time format from server");
    return;
  }

  TEST_INFO(NAME, "Server time: %04d-%02d-%02d %02d:%02d:%02d",
            serverTime.year, serverTime.month, serverTime.day,
            serverTime.hour, serverTime.minute, serverTime.second);

  // Step 4: Calculate delta
  int64_t delta = absDelta(serverTime, rtcTime);
  TEST_INFO(NAME, "Delta: %lld seconds", (long long)delta);

  // Step 5: Update RTC if needed
  if (delta > 60) {
    TEST_INFO(NAME, "Delta > 60s -> UPDATING RTC");
    DateTime newTime{};
    newTime.year   = serverTime.year;
    newTime.month  = serverTime.month;
    newTime.day    = serverTime.day;
    newTime.hour   = serverTime.hour;
    newTime.minute = serverTime.minute;
    newTime.second = serverTime.second;

    if (rtc.setTime(newTime)) {
      TEST_INFO(NAME, "RTC updated successfully");

      // Verify by reading back
      DateTime verify{};
      if (rtc.getTime(verify)) {
        TEST_INFO(NAME, "RTC verify: %04d-%02d-%02d %02d:%02d:%02d",
                  verify.year, verify.month, verify.day,
                  verify.hour, verify.minute, verify.second);
      }
      TEST_PASS(NAME);
    } else {
      TEST_FAIL(NAME, "RTC setTime failed");
    }
  } else {
    TEST_INFO(NAME, "Delta <= 60s -> RTC is already accurate, no update needed");
    TEST_PASS(NAME);
  }

  // Step 6: Disconnect
  if (connType == CONN_DCOM) dcomDisconnect();
  if (connType == CONN_SIM) simDisconnect();
}
