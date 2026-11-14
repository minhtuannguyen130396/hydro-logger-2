#include "services/connectivity/dcom_module.hpp"

#include <cstdio>
#include <cstring>

#include "common/config.hpp"
#include "services/net/http_client.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "modules/io/io_controller.hpp"

static const char* TAG = "DCOM";

namespace {

constexpr EventBits_t kWifiConnectedBit = BIT0;
constexpr EventBits_t kWifiFailBit = BIT1;

EventGroupHandle_t s_wifi_events = nullptr;
esp_netif_t* s_sta_netif = nullptr;
bool s_netif_ready = false;
bool s_handlers_ready = false;

char s_dcom_status[192] = "WIFI: Chua khoi dong";
char s_ip_str[32] = "";
uint8_t s_last_disconnect_reason = 0;

bool isFatalDisconnectReason(uint8_t reason) {
  switch (reason) {
    // Note: WIFI_REASON_NO_AP_FOUND is NOT fatal because DCOM/router may take
    // several seconds to boot and broadcast beacon after power-on.
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_CONNECTION_FAIL:
      return true;
    default:
      return false;
  }
}

const char* wifiReasonStr(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:          return "Khong tim thay AP / SSID";
    case WIFI_REASON_AUTH_FAIL:            return "Sai mat khau Wi-Fi (AUTH_FAIL)";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:    return "Loi xac thuc / Bat tay 4-way timeout";
    case WIFI_REASON_ASSOC_FAIL:           return "Lien ket voi AP that bai (Assoc fail)";
    case WIFI_REASON_CONNECTION_FAIL:      return "Ket noi Wi-Fi that bai";
    case WIFI_REASON_BEACON_TIMEOUT:       return "Mat tin hieu beacon AP (Beacon timeout)";
    default:                               return "Mat ket noi Wi-Fi";
  }
}

void wifiEventHandler(void*,
                      esp_event_base_t event_base,
                      int32_t event_id,
                      void* event_data) {
  if (!s_wifi_events) return;

  if (event_base == WIFI_EVENT) {
    if (event_id == WIFI_EVENT_STA_START) {
      esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
      xEventGroupClearBits(s_wifi_events, kWifiConnectedBit);

      auto* disconnected = static_cast<wifi_event_sta_disconnected_t*>(event_data);
      const uint8_t reason = disconnected ? disconnected->reason : 0;
      s_last_disconnect_reason = reason;
      ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%u (%s)", (unsigned)reason, wifiReasonStr(reason));

      if (isFatalDisconnectReason(reason)) {
        xEventGroupSetBits(s_wifi_events, kWifiFailBit);
      } else {
        esp_wifi_connect();
      }
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    auto* got_ip = static_cast<ip_event_got_ip_t*>(event_data);
    if (got_ip) {
      std::snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&got_ip->ip_info.ip));
    }
    xEventGroupSetBits(s_wifi_events, kWifiConnectedBit);
  }
}

bool ensureWifiRuntime(LogBuffer& log) {
  if (!s_netif_ready) {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      log.appendf("[DCOM] esp_netif_init FAIL err=0x%x\n", (unsigned)err);
      return false;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      log.appendf("[DCOM] event loop init FAIL err=0x%x\n", (unsigned)err);
      return false;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) {
      log.appendf("[DCOM] create wifi sta FAIL\n");
      return false;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      log.appendf("[DCOM] esp_wifi_init FAIL err=0x%x\n", (unsigned)err);
      return false;
    }

    // Use RAM storage only so stale NVS Wi-Fi cache (BSSID/channel) does not cause
    // connection failure across power cycles / reboots without a manual NVS erase.
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    s_netif_ready = true;
  }

  if (!s_wifi_events) {
    s_wifi_events = xEventGroupCreate();
    if (!s_wifi_events) {
      log.appendf("[DCOM] event group create FAIL\n");
      return false;
    }
  }

  if (!s_handlers_ready) {
    esp_err_t err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler, nullptr);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      log.appendf("[DCOM] wifi handler reg FAIL err=0x%x\n", (unsigned)err);
      return false;
    }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiEventHandler, nullptr);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      log.appendf("[DCOM] ip handler reg FAIL err=0x%x\n", (unsigned)err);
      return false;
    }
    s_handlers_ready = true;
  }

  return true;
}

} // namespace

const char* DcomModule::statusDetail() const {
  return s_dcom_status;
}

bool DcomModule::powerOn(LogBuffer& log) {
  std::snprintf(s_dcom_status, sizeof(s_dcom_status), "WIFI: Dang bat nguon...");
  log.appendf("[DCOM] power seq LOW->HIGH\n");
  IoController::instance().setDcomPower(false);
  vTaskDelay(pdMS_TO_TICKS(cfg::kDcomPowerEdgeDelayMs));
  IoController::instance().setDcomPower(true);
  log.appendf("[DCOM] wait boot %dms\n", (int)cfg::kDcomBootDelayMs);
  vTaskDelay(pdMS_TO_TICKS(cfg::kDcomBootDelayMs));
  return true;
}

bool DcomModule::checkInternet(uint32_t timeoutMs, LogBuffer& log) {
  if (!ensureWifiRuntime(log)) {
    std::snprintf(s_dcom_status, sizeof(s_dcom_status), "WIFI: ERROR - Khoi tao he thong Wi-Fi that bai");
    return false;
  }

  wifi_config_t wifi_cfg{};
  std::strncpy(reinterpret_cast<char*>(wifi_cfg.sta.ssid),
               cfg::kDcomWifiSsid,
               sizeof(wifi_cfg.sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(wifi_cfg.sta.password),
               cfg::kDcomWifiPassword,
               sizeof(wifi_cfg.sta.password) - 1);
  wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  wifi_cfg.sta.pmf_cfg.capable = true;
  wifi_cfg.sta.pmf_cfg.required = false;
  wifi_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  wifi_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

  xEventGroupClearBits(s_wifi_events, kWifiConnectedBit | kWifiFailBit);
  s_last_disconnect_reason = 0;
  s_ip_str[0] = '\0';

  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    log.appendf("[DCOM] set mode FAIL err=0x%x\n", (unsigned)err);
    std::snprintf(s_dcom_status, sizeof(s_dcom_status), "WIFI: ERROR - Set mode STA that bai (err=0x%x)", (unsigned)err);
    return false;
  }
  err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
  if (err != ESP_OK) {
    log.appendf("[DCOM] set config FAIL err=0x%x\n", (unsigned)err);
    std::snprintf(s_dcom_status, sizeof(s_dcom_status), "WIFI: ERROR - Cau hinh Wi-Fi that bai (err=0x%x)", (unsigned)err);
    return false;
  }

  if (!wifi_started_) {
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      log.appendf("[DCOM] wifi start FAIL err=0x%x\n", (unsigned)err);
      std::snprintf(s_dcom_status, sizeof(s_dcom_status), "WIFI: ERROR - Khoi dong Wi-Fi that bai (err=0x%x)", (unsigned)err);
      return false;
    }
    wifi_started_ = true;
  }

  err = esp_wifi_connect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
    log.appendf("[DCOM] wifi connect FAIL err=0x%x\n", (unsigned)err);
    std::snprintf(s_dcom_status, sizeof(s_dcom_status), "WIFI: ERROR - Goi lenh ket noi that bai (err=0x%x)", (unsigned)err);
    return false;
  }

  log.appendf("[DCOM] wait Wi-Fi ssid=%s timeout=%dms\n", cfg::kDcomWifiSsid, (int)timeoutMs);
  EventBits_t bits = xEventGroupWaitBits(
      s_wifi_events,
      kWifiConnectedBit | kWifiFailBit,
      pdTRUE,
      pdFALSE,
      pdMS_TO_TICKS(timeoutMs));

  const bool connected = (bits & kWifiConnectedBit) != 0;
  const bool failed = (bits & kWifiFailBit) != 0;
  log.appendf("[DCOM] wifi connected=%d\n", (int)connected);
  if (failed) {
    log.appendf("[DCOM] wifi connect FAIL (fatal disconnect)\n");
  } else if (!connected) {
    log.appendf("[DCOM] wifi connect TIMEOUT\n");
  }

  if (connected) {
    wifi_ap_record_t ap{};
    int rssi = 0;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
      rssi = ap.rssi;
    }
    std::snprintf(s_dcom_status, sizeof(s_dcom_status),
                  "WIFI: OK (SSID: %s, IP: %s, RSSI: %d dBm)",
                  cfg::kDcomWifiSsid,
                  s_ip_str[0] ? s_ip_str : "Connected",
                  rssi);
    log.appendf("[DCOM] wifi stabilize delay 5s\n");
    vTaskDelay(pdMS_TO_TICKS(5000));
  } else {
    if (s_last_disconnect_reason != 0) {
      std::snprintf(s_dcom_status, sizeof(s_dcom_status),
                    "WIFI: ERROR - %s (SSID: %s, reason=%u)",
                    wifiReasonStr(s_last_disconnect_reason),
                    cfg::kDcomWifiSsid,
                    (unsigned)s_last_disconnect_reason);
    } else {
      std::snprintf(s_dcom_status, sizeof(s_dcom_status),
                    "WIFI: ERROR - Het thoi gian cho ket noi / cap IP (Timeout %lums, SSID: %s)",
                    (unsigned long)timeoutMs, cfg::kDcomWifiSsid);
    }
  }

  return connected;
}

bool DcomModule::sendPayload(const std::string& url, const std::string& json, LogBuffer& log) {
  ESP_LOGI(TAG, "[HTTP:POST] url=%s bytes=%d", url.c_str(), (int)json.size());
  log.appendf("[DCOM] POST url=%s bytes=%d\n", url.c_str(), (int)json.size());

  bool ok = HttpClient::postJson(url, json, 8000);
  if (ok) {
    ESP_LOGI(TAG, "[HTTP:POST] OK");
    log.appendf("[DCOM] POST OK\n");
  } else {
    ESP_LOGW(TAG, "[HTTP:POST] FAIL");
    log.appendf("[DCOM] POST FAIL\n");
  }
  return ok;
}

void DcomModule::powerOff(LogBuffer& log) {
  if (wifi_started_) {
    (void)esp_wifi_disconnect();
    (void)esp_wifi_stop();
    wifi_started_ = false;
  }

  log.appendf("[DCOM] power idle LOW\n");
  IoController::instance().setDcomPower(false);
}
