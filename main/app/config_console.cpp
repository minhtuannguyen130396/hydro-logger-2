// Runtime configuration console.
//
// Reads simple "key:value" commands from the console UART (the same UART used by
// idf.py monitor, see CONFIG_ESP_CONSOLE_UART_NUM) and applies them on the fly.
// Started once from app_main and kept alive for the whole awake period, so the
// operator can configure the unit by typing into the serial monitor.
//
// Supported commands (one per line, terminated by Enter):
//   serial_number:TD_MW_0012   -> parse the trailing digits as the device code
//                                 ("mã máy") and persist it to NVS. The serial is
//                                 rebuilt from cfg::kDeviceSerialPrefix + code, so
//                                 it round-trips back to e.g. "TD_MW_0012".
#include "common/config.hpp"
#include "common/nvs_store.hpp"

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static const char* TAG = "ConfigConsole";

namespace {

// The console lives on the default UART (CONFIG_ESP_CONSOLE_UART_NUM, =0 here).
#if defined(CONFIG_ESP_CONSOLE_UART_NUM)
constexpr uart_port_t kConsoleUart = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
#else
constexpr uart_port_t kConsoleUart = UART_NUM_0;
#endif

constexpr size_t kMaxLineLen = 128;

std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace((unsigned char)s[b])) ++b;
  while (e > b && std::isspace((unsigned char)s[e - 1])) --e;
  return s.substr(b, e - b);
}

// Extract the device code ("mã máy") from a serial like "TD_MW_0012": take the
// trailing run of digits and parse it as a uint16. Returns false if there is no
// numeric suffix or it is out of range.
bool parseSerialCode(const std::string& value, uint16_t& code) {
  std::string v = trim(value);
  if (v.empty()) return false;

  size_t end = v.size();
  size_t start = end;
  while (start > 0 && std::isdigit((unsigned char)v[start - 1])) --start;
  if (start == end) return false;  // no trailing digits

  long n = std::strtol(v.c_str() + start, nullptr, 10);
  if (n < 0 || n > 65535) return false;
  code = (uint16_t)n;
  return true;
}

void handleSerialNumber(const std::string& value) {
  uint16_t code = 0;
  if (!parseSerialCode(value, code)) {
    ESP_LOGW(TAG, "serial_number: invalid value '%s' (expected e.g. TD_MW_0012)",
             value.c_str());
    return;
  }

  // Inform if the prefix differs from the firmware's expectation; the stored
  // serial is always rebuilt as cfg::kDeviceSerialPrefix + code on read.
  std::string v = trim(value);
  const size_t pfxLen = std::strlen(cfg::kDeviceSerialPrefix);
  if (v.compare(0, pfxLen, cfg::kDeviceSerialPrefix) != 0) {
    ESP_LOGW(TAG, "serial_number: prefix '%.*s' != '%s'; only the code is stored",
             (int)pfxLen, v.c_str(), cfg::kDeviceSerialPrefix);
  }

  NvsStore::setDeviceCode(code);

  char readback[32]{};
  NvsStore::getDeviceSerial(readback, sizeof(readback));
  ESP_LOGI(TAG, "serial_number saved -> %s (code=%u)", readback, (unsigned)code);
  std::printf("[OK] serial_number = %s (code=%u)\r\n", readback, (unsigned)code);
}

void handleLine(const std::string& rawLine) {
  std::string line = trim(rawLine);
  if (line.empty()) return;

  size_t colon = line.find(':');
  if (colon == std::string::npos) {
    return;  // not a key:value command — ignore quietly
  }

  std::string cmd = trim(line.substr(0, colon));
  std::string val = trim(line.substr(colon + 1));

  if (cmd == "serial_number") {
    handleSerialNumber(val);
  } else {
    ESP_LOGW(TAG, "unknown command '%s'", cmd.c_str());
  }
}

void console_task(void*) {
  // Install the UART driver on the console port so we can read keystrokes. Log
  // output continues to flow through the standard console write path unaffected.
  if (!uart_is_driver_installed(kConsoleUart)) {
    if (uart_driver_install(kConsoleUart, 256, 0, 0, nullptr, 0) != ESP_OK) {
      ESP_LOGW(TAG, "uart_driver_install failed, console disabled");
      vTaskDelete(nullptr);
      return;
    }
  }

  ESP_LOGI(TAG, "ready - type 'serial_number:TD_MW_0012' to set device code");

  std::string line;
  uint8_t ch = 0;
  for (;;) {
    int n = uart_read_bytes(kConsoleUart, &ch, 1, pdMS_TO_TICKS(100));
    if (n != 1) continue;

    if (ch == '\r' || ch == '\n') {
      handleLine(line);
      line.clear();
    } else if (ch == 0x08 || ch == 0x7f) {  // backspace / delete
      if (!line.empty()) line.pop_back();
    } else if (line.size() < kMaxLineLen) {
      line.push_back((char)ch);
    }
  }
}

}  // namespace

// Start the configuration console task. Safe to call once at boot.
void config_console_start() {
  xTaskCreate(&console_task, "config_console", 4096, nullptr, 4, nullptr);
}
