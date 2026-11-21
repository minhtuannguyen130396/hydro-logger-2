// Runtime configuration console.
//
// Reads simple "key:value" commands from UART0 and applies them on the fly.
// Started once from app_main and kept alive for the whole awake period, so the
// operator can configure the unit by typing into the serial monitor.
//
// Works independently of ESP-IDF console logging status (even if CONFIG_ESP_CONSOLE_NONE=y
// or all logs are disabled, UART0 is initialized directly for command I/O).
//
// Supported commands (one per line, terminated by Enter):
//   serial_number:TD_MW_0012   -> parse the trailing digits as the device code
//                                 ("mã máy") and persist it to NVS. The serial is
//                                 rebuilt from cfg::kDeviceSerialPrefix + code, so
//                                 it round-trips back to e.g. "TD_MW_0012".
//   serial_number?             -> query current serial number and code.
#include "common/config.hpp"
#include "common/nvs_store.hpp"

#include "driver/gpio.h"
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

// Luon su dung UART0 bat ke che do console logging cua he thong bat hay tat
constexpr uart_port_t kConsoleUart = UART_NUM_0;

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
    char errResp[128];
    int len = std::snprintf(errResp, sizeof(errResp),
                            "[ERR] serial_number: invalid value '%s' (expected e.g. TD_MW_0012)\r\n",
                            value.c_str());
    uart_write_bytes(kConsoleUart, errResp, len);
    ESP_LOGW(TAG, "serial_number: invalid value '%s' (expected e.g. TD_MW_0012)",
             value.c_str());
    return;
  }

  // Inform if the prefix differs from the firmware's expectation; the stored
  // serial is always rebuilt as cfg::kDeviceSerialPrefix + code on read.
  std::string v = trim(value);
  const size_t pfxLen = std::strlen(cfg::kDeviceSerialPrefix);
  if (v.compare(0, pfxLen, cfg::kDeviceSerialPrefix) != 0) {
    char warnResp[128];
    int len = std::snprintf(warnResp, sizeof(warnResp),
                            "[WARN] prefix '%.*s' != '%s'; code %u will be saved with prefix '%s'\r\n",
                            (int)pfxLen, v.c_str(), cfg::kDeviceSerialPrefix, (unsigned)code, cfg::kDeviceSerialPrefix);
    uart_write_bytes(kConsoleUart, warnResp, len);
    ESP_LOGW(TAG, "serial_number: prefix '%.*s' != '%s'; only the code is stored",
             (int)pfxLen, v.c_str(), cfg::kDeviceSerialPrefix);
  }

  NvsStore::setDeviceCode(code);

  char readback[32]{};
  NvsStore::getDeviceSerial(readback, sizeof(readback));
  char okResp[128];
  int len = std::snprintf(okResp, sizeof(okResp),
                          "[OK] serial_number = %s (code=%u)\r\n",
                          readback, (unsigned)code);
  uart_write_bytes(kConsoleUart, okResp, len);
  std::printf("%s", okResp);
  ESP_LOGI(TAG, "serial_number saved -> %s (code=%u)", readback, (unsigned)code);
}

void handleLine(const std::string& rawLine) {
  std::string line = trim(rawLine);
  if (line.empty()) return;

  size_t colon = line.find(':');
  if (colon == std::string::npos) {
    colon = line.find('=');
  }

  std::string cmd;
  std::string val;
  if (colon != std::string::npos) {
    cmd = trim(line.substr(0, colon));
    val = trim(line.substr(colon + 1));
  } else {
    size_t space = line.find(' ');
    if (space != std::string::npos) {
      cmd = trim(line.substr(0, space));
      val = trim(line.substr(space + 1));
    } else {
      cmd = line;
    }
  }

  if (cmd == "serial_number" || cmd == "serial") {
    if (val.empty() || val == "?") {
      char readback[32]{};
      NvsStore::getDeviceSerial(readback, sizeof(readback));
      uint16_t code = NvsStore::getDeviceCode(cfg::kDefaultDeviceCode);
      char resp[128];
      int n = std::snprintf(resp, sizeof(resp),
                            "[CURRENT] serial_number = %s (code=%u)\r\n",
                            readback, (unsigned)code);
      uart_write_bytes(kConsoleUart, resp, n);
    } else {
      handleSerialNumber(val);
    }
  } else {
    char unk[128];
    int n = std::snprintf(unk, sizeof(unk),
                          "[ERR] unknown command '%s' (try 'serial_number:TD_MW_0012')\r\n",
                          cmd.c_str());
    uart_write_bytes(kConsoleUart, unk, n);
    ESP_LOGW(TAG, "unknown command '%s'", cmd.c_str());
  }
}

void console_task(void*) {
  // Khoi tao UART0 driver truc tiep neu he thong tat UART console logging (CONFIG_ESP_CONSOLE_NONE)
  if (!uart_is_driver_installed(kConsoleUart)) {
    uart_config_t uart_cfg{};
    uart_cfg.baud_rate = 115200;
    uart_cfg.data_bits = UART_DATA_8_BITS;
    uart_cfg.parity    = UART_PARITY_DISABLE;
    uart_cfg.stop_bits = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk = UART_SCLK_DEFAULT;

    esp_err_t err = uart_param_config(kConsoleUart, &uart_cfg);
    if (err == ESP_OK) {
      uart_set_pin(kConsoleUart, GPIO_NUM_1, GPIO_NUM_3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
      err = uart_driver_install(kConsoleUart, 512, 512, 0, nullptr, 0);
    }

    if (err != ESP_OK) {
      ESP_LOGW(TAG, "uart_driver_install failed err=%d, console disabled", (int)err);
      vTaskDelete(nullptr);
      return;
    }
  }

  const char* prompt = "\r\n[ConfigConsole] UART0 active - type 'serial_number:TD_MW_0012' to configure\r\n";
  uart_write_bytes(kConsoleUart, prompt, std::strlen(prompt));
  ESP_LOGI(TAG, "ready - type 'serial_number:TD_MW_0012' to set device code");

  std::string line;
  uint8_t ch = 0;
  for (;;) {
    int n = uart_read_bytes(kConsoleUart, &ch, 1, pdMS_TO_TICKS(100));
    if (n != 1) continue;

    if (ch == '\r' || ch == '\n') {
      const char* crlf = "\r\n";
      uart_write_bytes(kConsoleUart, crlf, 2);
      handleLine(line);
      line.clear();
    } else if (ch == 0x08 || ch == 0x7f) {  // backspace / delete
      if (!line.empty()) {
        line.pop_back();
        const char* bs = "\b \b";
        uart_write_bytes(kConsoleUart, bs, 3);
      }
    } else if (line.size() < kMaxLineLen && std::isprint(ch)) {
      line.push_back((char)ch);
      uart_write_bytes(kConsoleUart, &ch, 1);  // local echo
    }
  }
}

}  // namespace

// Start the configuration console task. Safe to call once at boot.
void config_console_start() {
  xTaskCreate(&console_task, "config_console", 4096, nullptr, 4, nullptr);
}
