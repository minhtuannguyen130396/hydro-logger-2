#include "services/net/http_client.hpp"
#include "esp_http_client.h"
#include "esp_log.h"

static const char* TAG = "HttpClient";

static bool is2xx(int code) { return code >= 200 && code < 300; }

bool HttpClient::postJson(const std::string& url, const std::string& json, uint32_t timeoutMs) {
  esp_http_client_config_t cfg{};
  cfg.url = url.c_str();
  cfg.timeout_ms = (int)timeoutMs;

  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return false;

  esp_http_client_set_method(c, HTTP_METHOD_POST);
  esp_http_client_set_header(c, "Content-Type", "application/json");
  esp_http_client_set_post_field(c, json.c_str(), (int)json.size());

  esp_err_t err = esp_http_client_perform(c);
  int code = esp_http_client_get_status_code(c);

  esp_http_client_cleanup(c);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "POST failed: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(TAG, "POST code=%d", code);
  return is2xx(code);
}

bool HttpClient::getText(const std::string& url, std::string& out, uint32_t timeoutMs) {
  out.clear();
  esp_http_client_config_t cfg{};
  cfg.url = url.c_str();
  cfg.timeout_ms = (int)timeoutMs;

  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) return false;

  esp_http_client_set_method(c, HTTP_METHOD_GET);
  esp_err_t err = esp_http_client_perform(c);
  int code = esp_http_client_get_status_code(c);

  if (err != ESP_OK || !is2xx(code)) {
    ESP_LOGW(TAG, "GET failed err=%s code=%d", esp_err_to_name(err), code);
    esp_http_client_cleanup(c);
    return false;
  }

  int len = esp_http_client_get_content_length(c);
  if (len < 0) len = 1024;
  out.reserve((size_t)len);

  char buf[256];
  int r = 0;
  while ((r = esp_http_client_read(c, buf, sizeof(buf))) > 0) {
    out.append(buf, buf + r);
  }
  esp_http_client_cleanup(c);
  return true;
}
