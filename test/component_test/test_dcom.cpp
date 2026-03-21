#include "test_common.hpp"
#include "modules/io/io_controller.hpp"
#include "common/config.hpp"

#include <cstring>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

static const char* NAME = "DCOM";

static constexpr EventBits_t kConnectedBit = BIT0;
static EventGroupHandle_t s_events = nullptr;

static void wifiHandler(void*, esp_event_base_t base, int32_t id, void*) {
  if (!s_events) return;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    auto* event = (ip_event_got_ip_t*)nullptr; // just set the bit
    (void)event;
    xEventGroupSetBits(s_events, kConnectedBit);
  }
}

void test_dcom() {
  TEST_START(NAME);

  // Power on DCOM module
  TEST_INFO(NAME, "Power ON (LOW -> HIGH)");
  IoController::instance().setDcomPower(false);
  testDelayMs(100);
  IoController::instance().setDcomPower(true);
  testDelayMs(500);

  // Init Wi-Fi stack
  TEST_INFO(NAME, "Init Wi-Fi stack...");

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    TEST_FAIL(NAME, "esp_netif_init failed");
    IoController::instance().setDcomPower(false);
    return;
  }

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    TEST_FAIL(NAME, "event loop create failed");
    IoController::instance().setDcomPower(false);
    return;
  }

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&init_cfg);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    TEST_FAIL(NAME, "esp_wifi_init failed");
    IoController::instance().setDcomPower(false);
    return;
  }

  s_events = xEventGroupCreate();
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiHandler, nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiHandler, nullptr);

  // Configure
  wifi_config_t wifi_cfg{};
  strncpy(reinterpret_cast<char*>(wifi_cfg.sta.ssid),
          cfg::kDcomWifiSsid, sizeof(wifi_cfg.sta.ssid) - 1);
  strncpy(reinterpret_cast<char*>(wifi_cfg.sta.password),
          cfg::kDcomWifiPassword, sizeof(wifi_cfg.sta.password) - 1);
  wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
  esp_wifi_start();

  TEST_INFO(NAME, "Connecting to SSID='%s'...", cfg::kDcomWifiSsid);

  // Wait for connection (30s timeout)
  xEventGroupClearBits(s_events, kConnectedBit);
  esp_wifi_connect();

  EventBits_t bits = xEventGroupWaitBits(
      s_events, kConnectedBit, pdTRUE, pdFALSE, pdMS_TO_TICKS(30000));

  bool connected = (bits & kConnectedBit) != 0;

  if (connected) {
    TEST_INFO(NAME, "Wi-Fi connected!");

    // Get IP info
    esp_netif_ip_info_t ip_info{};
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
      TEST_INFO(NAME, "IP: " IPSTR, IP2STR(&ip_info.ip));
      TEST_INFO(NAME, "GW: " IPSTR, IP2STR(&ip_info.gw));
    }

    TEST_PASS(NAME);
  } else {
    TEST_FAIL(NAME, "Wi-Fi connection timeout (30s)");
  }

  // Cleanup
  TEST_INFO(NAME, "Disconnecting Wi-Fi...");
  esp_wifi_disconnect();
  esp_wifi_stop();

  TEST_INFO(NAME, "Power OFF");
  IoController::instance().setDcomPower(false);

  if (s_events) {
    vEventGroupDelete(s_events);
    s_events = nullptr;
  }
}
