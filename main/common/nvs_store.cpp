#include "nvs_store.hpp"
#include "common/config.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <cstdio>

static const char* TAG = "NvsStore";
static const char* NS  = "wl";

// --- Key names ---
static const char* KEY_LAST_COMM    = "last_com";
static const char* KEY_LAST_SIM_APN = "sim_apn";
static const char* KEY_DEV_CODE     = "dev_code";
static const char* KEY_WL_OFFSET    = "wl_off";
static const char* KEY_VOL_K        = "vol_k";

// ============================================================
// Generic NVS helpers
// ============================================================
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

static uint16_t get_u16(const char* key, uint16_t defv) {
  nvs_handle_t h{};
  if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return defv;
  uint16_t v = defv;
  nvs_get_u16(h, key, &v);
  nvs_close(h);
  return v;
}
static void set_u16(const char* key, uint16_t v) {
  nvs_handle_t h{};
  if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u16(h, key, v);
  nvs_commit(h);
  nvs_close(h);
}

static int32_t get_i32(const char* key, int32_t defv) {
  nvs_handle_t h{};
  if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return defv;
  int32_t v = defv;
  nvs_get_i32(h, key, &v);
  nvs_close(h);
  return v;
}
static void set_i32(const char* key, int32_t v) {
  nvs_handle_t h{};
  if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_i32(h, key, v);
  nvs_commit(h);
  nvs_close(h);
}

static uint32_t get_u32(const char* key, uint32_t defv) {
  nvs_handle_t h{};
  if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return defv;
  uint32_t v = defv;
  nvs_get_u32(h, key, &v);
  nvs_close(h);
  return v;
}
static void set_u32(const char* key, uint32_t v) {
  nvs_handle_t h{};
  if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u32(h, key, v);
  nvs_commit(h);
  nvs_close(h);
}

// ============================================================
// Init
// ============================================================
bool NvsStore::init() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_LOGI(TAG, "nvs init: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

// ============================================================
// Connectivity preferences
// ============================================================
CommType NvsStore::getLastSuccessComm(CommType def) {
  return static_cast<CommType>(get_u8(KEY_LAST_COMM, static_cast<uint8_t>(def)));
}
void NvsStore::setLastSuccessComm(CommType t) {
  set_u8(KEY_LAST_COMM, static_cast<uint8_t>(t));
}

SimApnProfile NvsStore::getLastSimApn(SimApnProfile def) {
  return static_cast<SimApnProfile>(get_u8(KEY_LAST_SIM_APN, static_cast<uint8_t>(def)));
}
void NvsStore::setLastSimApn(SimApnProfile profile) {
  set_u8(KEY_LAST_SIM_APN, static_cast<uint8_t>(profile));
}

// ============================================================
// Device code / serial
// ============================================================
uint16_t NvsStore::getDeviceCode(uint16_t def) {
  return get_u16(KEY_DEV_CODE, def);
}
void NvsStore::setDeviceCode(uint16_t code) {
  set_u16(KEY_DEV_CODE, code);
  ESP_LOGI(TAG, "saved device_code=%u", (unsigned)code);
}

const char* NvsStore::getDeviceSerial(char* out, int maxLen) {
  uint16_t code = getDeviceCode(cfg::kDefaultDeviceCode);
  std::snprintf(out, maxLen, "%s%05u", cfg::kDeviceSerialPrefix, (unsigned)code);
  return out;
}

// ============================================================
// Water-level offset
// ============================================================
int32_t NvsStore::getWaterLevelOffset(int32_t def) {
  return get_i32(KEY_WL_OFFSET, def);
}
void NvsStore::setWaterLevelOffset(int32_t offset) {
  set_i32(KEY_WL_OFFSET, offset);
  ESP_LOGI(TAG, "saved wl_offset=%d mm", (int)offset);
}

// ============================================================
// Voltage calibration factor K (stored as K * 1000)
// ============================================================
uint32_t NvsStore::getVoltageK(uint32_t def) {
  return get_u32(KEY_VOL_K, def);
}
void NvsStore::setVoltageK(uint32_t k_x1000) {
  set_u32(KEY_VOL_K, k_x1000);
  ESP_LOGI(TAG, "saved voltage_k=%u (x1000)", (unsigned)k_x1000);
}
