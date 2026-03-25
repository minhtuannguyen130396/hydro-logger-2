#include "services/connectivity/dcom_module.hpp"

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

bool isFatalDisconnectReason(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_CONNECTION_FAIL:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
      return true;
    default:
      return false;
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
      ESP_LOGW(TAG, "Wi-Fi disconnected, reason=%u", (unsigned)reason);

      if (isFatalDisconnectReason(reason)) {
        xEventGroupSetBits(s_wifi_events, kWifiFailBit);
      } else {
        esp_wifi_connect();
      }
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
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

bool DcomModule::powerOn(LogBuffer& log) {
  log.appendf("[DCOM] power seq LOW->HIGH\n");
  IoController::instance().setDcomPower(false);
  vTaskDelay(pdMS_TO_TICKS(cfg::kDcomPowerEdgeDelayMs));
  IoController::instance().setDcomPower(true);
  log.appendf("[DCOM] wait boot %dms\n", (int)cfg::kDcomBootDelayMs);
  vTaskDelay(pdMS_TO_TICKS(cfg::kDcomBootDelayMs));
  return true;
}

bool DcomModule::checkInternet(uint32_t timeoutMs, LogBuffer& log) {
  if (!ensureWifiRuntime(log)) return false;

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

  xEventGroupClearBits(s_wifi_events, kWifiConnectedBit | kWifiFailBit);

  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    log.appendf("[DCOM] set mode FAIL err=0x%x\n", (unsigned)err);
    return false;
  }
  err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
  if (err != ESP_OK) {
    log.appendf("[DCOM] set config FAIL err=0x%x\n", (unsigned)err);
    return false;
  }

  if (!wifi_started_) {
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      log.appendf("[DCOM] wifi start FAIL err=0x%x\n", (unsigned)err);
      return false;
    }
    wifi_started_ = true;
  }

  err = esp_wifi_connect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
    log.appendf("[DCOM] wifi connect FAIL err=0x%x\n", (unsigned)err);
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
    log.appendf("[DCOM] wifi stabilize delay 5s\n");
    vTaskDelay(pdMS_TO_TICKS(5000));
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
