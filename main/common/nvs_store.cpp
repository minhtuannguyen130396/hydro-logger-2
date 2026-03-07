#include "nvs_store.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "NvsStore";
static const char* NS  = "wl";
static const char* KEY_LAST_SENSOR = "last_sen";
static const char* KEY_LAST_COMM   = "last_com";

bool NvsStore::init() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_LOGI(TAG, "nvs init: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

static uint8_t get_u8(const char* key, uint8_t defv) {
  nvs_handle_t h{};
  if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return defv;
  uint8_t v = defv;
  nvs_get_u8(h, key, &v);
  nvs_close(h);
  return v;
}
static void set_u8(const char* key, uint8_t v) {
  nvs_handle_t h{};
  if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u8(h, key, v);
  nvs_commit(h);
  nvs_close(h);
}

SensorType NvsStore::getLastWorkingSensor(SensorType def) {
  return static_cast<SensorType>(get_u8(KEY_LAST_SENSOR, static_cast<uint8_t>(def)));
}
void NvsStore::setLastWorkingSensor(SensorType t) {
  set_u8(KEY_LAST_SENSOR, static_cast<uint8_t>(t));
}

CommType NvsStore::getLastSuccessComm(CommType def) {
  return static_cast<CommType>(get_u8(KEY_LAST_COMM, static_cast<uint8_t>(def)));
}
void NvsStore::setLastSuccessComm(CommType t) {
  set_u8(KEY_LAST_COMM, static_cast<uint8_t>(t));
}
