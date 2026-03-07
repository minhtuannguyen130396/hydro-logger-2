#include "services/connectivity/sim4g_module.hpp"
#include "modules/io/io_controller.hpp"
#include "board/uart_drv.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "Sim4G";

bool Sim4GModule::sendAtOk(const char* cmd, uint32_t timeoutMs, LogBuffer& log) {
  log.appendf("[SIM] AT>%s\n", cmd);
  UartDrv::writeLineSim(cmd);

  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    auto line = UartDrv::readLineSim(200);
    if (!line.empty()) {
      log.appendf("[SIM] <%s\n", line.c_str());
      if (line.find("OK") != std::string::npos) return true;
      if (line.find("ERROR") != std::string::npos) return false;
    }
  }
  log.appendf("[SIM] timeout waiting OK\n");
  return false;
}

bool Sim4GModule::powerOn(LogBuffer& log) {
  log.appendf("[SIM] power on\n");
  IoController::instance().setSimPower(true);
  vTaskDelay(pdMS_TO_TICKS(800));

  // Init UART once
  static bool uart_ok = false;
  if (!uart_ok) uart_ok = UartDrv::initSimUart();
  if (!uart_ok) {
    log.appendf("[SIM] uart init FAIL\n");
    return false;
  }

  // Basic AT handshake
  return sendAtOk("AT", 1000, log);
}

bool Sim4GModule::checkInternet(uint32_t timeoutMs, LogBuffer& log) {
  // TODO: Replace by real attach/APN/PDP/HTTP ping checks for your modem.
  // Stub: try a few common commands.
  bool ok1 = sendAtOk("ATE0", 1000, log);
  bool ok2 = sendAtOk("AT+CSQ", 1000, log);
  bool ok3 = sendAtOk("AT+CREG?", 1500, log);

  // If those OK, assume internet ready (placeholder).
  bool ok = ok1 && ok2 && ok3;
  log.appendf("[SIM] checkInternet=%d (stub)\n", (int)ok);

  // Simulate some waiting up to timeoutMs
  vTaskDelay(pdMS_TO_TICKS(timeoutMs/4));
  return ok;
}

bool Sim4GModule::sendPayload(const std::string& json, LogBuffer& log) {
  // TODO: Use module TCP/HTTP stack, or switch to PPP + esp_http_client over lwIP.
  // Placeholder: just log.
  log.appendf("[SIM] sendPayload bytes=%d (stub)\n", (int)json.size());
  ESP_LOGI(TAG, "payload: %.*s", (int)json.size(), json.c_str());
  return true;
}

void Sim4GModule::powerOff(LogBuffer& log) {
  log.appendf("[SIM] power off\n");
  IoController::instance().setSimPower(false);
}
