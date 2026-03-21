#include "services/connectivity/sim4g_module.hpp"

#include <algorithm>
#include <cstdio>

#include "board/uart_drv.hpp"
#include "common/config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modules/io/io_controller.hpp"

namespace {

struct HttpActionResult {
  int method{0};
  int status{0};
  int body_len{0};
};

bool is2xx(int status) {
  return status >= 200 && status < 300;
}

bool parseHttpActionLine(const std::string& line, HttpActionResult& out) {
  int method = 0;
  int status = 0;
  int body_len = 0;
  if (std::sscanf(line.c_str(), "+HTTPACTION: %d,%d,%d", &method, &status, &body_len) == 3 ||
      std::sscanf(line.c_str(), "+HTTPACTION:%d,%d,%d", &method, &status, &body_len) == 3) {
    out.method = method;
    out.status = status;
    out.body_len = body_len;
    return true;
  }
  return false;
}

uint32_t splitBudget(uint32_t totalMs) {
  return std::max<uint32_t>(4000, totalMs / 2);
}

} // namespace

const char* Sim4GModule::apnName(SimApnProfile profile) const {
  return (profile == SimApnProfile::Viettel) ? "Viettel" : "VinaPhone";
}

const char* Sim4GModule::apnString(SimApnProfile profile) const {
  return (profile == SimApnProfile::Viettel) ? cfg::kSimApnViettel : cfg::kSimApnVinaphone;
}

SimApnProfile Sim4GModule::otherApn(SimApnProfile profile) const {
  return (profile == SimApnProfile::Viettel) ? SimApnProfile::Vinaphone : SimApnProfile::Viettel;
}

bool Sim4GModule::waitForToken(const char* expect,
                               uint32_t timeoutMs,
                               LogBuffer& log,
                               std::string* matched) {
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    std::string line = UartDrv::readLineSim(200);
    if (line.empty()) continue;

    log.appendf("[SIM] <%s\n", line.c_str());
    if (line.find(expect) != std::string::npos) {
      if (matched) *matched = line;
      return true;
    }
    if (line.find("ERROR") != std::string::npos) {
      return false;
    }
  }
  log.appendf("[SIM] timeout waiting %s\n", expect);
  return false;
}

bool Sim4GModule::sendAtExpect(const char* cmd,
                               const char* expect,
                               uint32_t timeoutMs,
                               LogBuffer& log) {
  UartDrv::flushSim();
  log.appendf("[SIM] AT>%s\n", cmd);
  if (!UartDrv::writeLineSim(cmd)) {
    log.appendf("[SIM] uart write FAIL\n");
    return false;
  }
  return waitForToken(expect, timeoutMs, log);
}

bool Sim4GModule::sendAtOk(const char* cmd, uint32_t timeoutMs, LogBuffer& log) {
  return sendAtExpect(cmd, "OK", timeoutMs, log);
}

bool Sim4GModule::ensureReadyForAt(LogBuffer& log) {
  static bool uart_ok = false;
  if (!uart_ok) {
    uart_ok = UartDrv::initSimUart();
  }
  if (!uart_ok) {
    log.appendf("[SIM] uart init FAIL\n");
    return false;
  }

  const uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < cfg::kSimBootDelayMs) {
    if (sendAtOk("AT", 1000, log)) {
      log.appendf("[SIM] AT ready\n");
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(cfg::kSimAtRetryDelayMs));
  }

  log.appendf("[SIM] AT handshake FAIL\n");
  return false;
}

bool Sim4GModule::powerOn(LogBuffer& log) {
  log.appendf("[SIM] power seq HIGH->LOW\n");
  IoController::instance().setSimPower(true);
  vTaskDelay(pdMS_TO_TICKS(cfg::kSimPowerEdgeDelayMs));
  IoController::instance().setSimPower(false);
  log.appendf("[SIM] wait boot %dms\n", (int)cfg::kSimBootDelayMs);
  vTaskDelay(pdMS_TO_TICKS(cfg::kSimBootDelayMs));

  active_ = ensureReadyForAt(log);
  if (!active_) {
    IoController::instance().setSimPower(true);
  }
  return active_;
}

bool Sim4GModule::configureApn(SimApnProfile profile, uint32_t timeoutMs, LogBuffer& log) {
  const uint32_t start = (uint32_t)xTaskGetTickCount();
  char cmd[96];

  log.appendf("[SIM] try APN profile=%s apn=%s\n", apnName(profile), apnString(profile));

  if (!sendAtOk("ATE0", 1000, log)) return false;
  if (!sendAtExpect("AT+CPIN?", "READY", 2000, log)) return false;
  if (!sendAtOk("AT+CSQ", 1000, log)) return false;
  if (!sendAtOk("AT+CGATT=1", 5000, log)) return false;

  std::snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apnString(profile));
  if (!sendAtOk(cmd, 3000, log)) return false;

  if (!sendAtOk("AT+CGAUTH=1,0", 2000, log)) return false;
  if (!sendAtOk("AT+CGACT=1,1", 5000, log)) return false;
  if (!sendAtExpect("AT+CGATT?", "+CGATT: 1", 2000, log)) return false;
  if (!sendAtExpect("AT+CGACT?", "+CGACT: 1,1", 3000, log)) return false;

  log.appendf("[SIM] APN profile=%s ready in %dms\n",
              apnName(profile),
              (int)pdTICKS_TO_MS(xTaskGetTickCount() - start));
  (void)timeoutMs;
  return true;
}

bool Sim4GModule::checkInternet(uint32_t timeoutMs, LogBuffer& log) {
  if (!active_ && !ensureReadyForAt(log)) {
    return false;
  }

  const SimApnProfile first = NvsStore::getLastSimApn(SimApnProfile::Viettel);
  const SimApnProfile second = otherApn(first);
  const uint32_t firstBudget = splitBudget(timeoutMs);
  const uint32_t secondBudget = std::max<uint32_t>(4000, timeoutMs - std::min(timeoutMs, firstBudget));

  if (configureApn(first, firstBudget, log)) {
    active_apn_ = first;
    NvsStore::setLastSimApn(active_apn_);
    log.appendf("[SIM] save APN profile=%s\n", apnName(active_apn_));
    return true;
  }

  log.appendf("[SIM] APN profile=%s FAIL -> switch\n", apnName(first));
  if (configureApn(second, secondBudget, log)) {
    active_apn_ = second;
    NvsStore::setLastSimApn(active_apn_);
    log.appendf("[SIM] save APN profile=%s\n", apnName(active_apn_));
    return true;
  }

  log.appendf("[SIM] both APN profiles FAIL\n");
  return false;
}

bool Sim4GModule::sendPayload(const std::string& url, const std::string& json, LogBuffer& log) {
  if (!active_) {
    log.appendf("[SIM] sendPayload while module inactive\n");
    return false;
  }

  std::string actionLine;
  HttpActionResult result{};
  char cmd[320];

  (void)sendAtOk("AT+HTTPTERM", 1000, log);
  if (!sendAtOk("AT+HTTPINIT", 2000, log)) return false;
  if (!sendAtOk("AT+HTTPPARA=\"CID\",1", 1000, log)) return false;

  std::snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url.c_str());
  if (!sendAtOk(cmd, 2000, log)) return false;
  if (!sendAtOk("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 1000, log)) return false;

  std::snprintf(cmd, sizeof(cmd), "AT+HTTPDATA=%d,%d", (int)json.size(), (int)cfg::kSimHttpDataTimeoutMs);
  if (!sendAtExpect(cmd, "DOWNLOAD", cfg::kSimHttpDataTimeoutMs, log)) {
    (void)sendAtOk("AT+HTTPTERM", 1000, log);
    return false;
  }

  log.appendf("[SIM] TX payload bytes=%d\n", (int)json.size());
  if (UartDrv::writeSim(reinterpret_cast<const uint8_t*>(json.data()), (int)json.size()) !=
      (int)json.size()) {
    log.appendf("[SIM] payload write FAIL\n");
    (void)sendAtOk("AT+HTTPTERM", 1000, log);
    return false;
  }
  if (!waitForToken("OK", cfg::kSimHttpDataTimeoutMs, log)) {
    (void)sendAtOk("AT+HTTPTERM", 1000, log);
    return false;
  }

  UartDrv::flushSim();
  log.appendf("[SIM] AT>AT+HTTPACTION=1\n");
  if (!UartDrv::writeLineSim("AT+HTTPACTION=1")) {
    log.appendf("[SIM] uart write FAIL\n");
    (void)sendAtOk("AT+HTTPTERM", 1000, log);
    return false;
  }
  if (!waitForToken("OK", 2000, log)) {
    (void)sendAtOk("AT+HTTPTERM", 1000, log);
    return false;
  }
  if (!waitForToken("+HTTPACTION:", cfg::kSimHttpActionTimeoutMs, log, &actionLine) ||
      !parseHttpActionLine(actionLine, result)) {
    (void)sendAtOk("AT+HTTPTERM", 1000, log);
    return false;
  }

  log.appendf("[SIM] HTTP status=%d body=%d\n", result.status, result.body_len);
  (void)sendAtOk("AT+HTTPTERM", 1000, log);
  return is2xx(result.status);
}

bool Sim4GModule::httpGet(const std::string& url, std::string& response, LogBuffer& log) {
  if (!active_) {
    log.appendf("[SIM] httpGet while module inactive\n");
    return false;
  }

  std::string actionLine;
  HttpActionResult result{};
  char cmd[320];

  (void)sendAtOk("AT+HTTPTERM", 1000, log);
  if (!sendAtOk("AT+HTTPINIT", 2000, log)) return false;
  if (!sendAtOk("AT+HTTPPARA=\"CID\",1", 1000, log)) return false;

  std::snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url.c_str());
  if (!sendAtOk(cmd, 2000, log)) return false;

  // HTTP GET (method 0)
  UartDrv::flushSim();
  log.appendf("[SIM] AT>AT+HTTPACTION=0\n");
  if (!UartDrv::writeLineSim("AT+HTTPACTION=0")) {
    log.appendf("[SIM] uart write FAIL\n");
    (void)sendAtOk("AT+HTTPTERM", 1000, log);
    return false;
  }
  if (!waitForToken("OK", 2000, log)) {
    (void)sendAtOk("AT+HTTPTERM", 1000, log);
    return false;
  }
  if (!waitForToken("+HTTPACTION:", cfg::kSimHttpActionTimeoutMs, log, &actionLine) ||
      !parseHttpActionLine(actionLine, result)) {
    (void)sendAtOk("AT+HTTPTERM", 1000, log);
    return false;
  }

  log.appendf("[SIM] HTTP GET status=%d body=%d\n", result.status, result.body_len);
  if (!is2xx(result.status) || result.body_len <= 0) {
    (void)sendAtOk("AT+HTTPTERM", 1000, log);
    return false;
  }

  // Read response body
  std::snprintf(cmd, sizeof(cmd), "AT+HTTPREAD=0,%d", result.body_len);
  UartDrv::flushSim();
  log.appendf("[SIM] AT>%s\n", cmd);
  if (!UartDrv::writeLineSim(cmd)) {
    (void)sendAtOk("AT+HTTPTERM", 1000, log);
    return false;
  }

  // Wait for +HTTPREAD: <len> then read the body lines
  if (!waitForToken("+HTTPREAD:", 5000, log)) {
    (void)sendAtOk("AT+HTTPTERM", 1000, log);
    return false;
  }

  // Next line(s) contain the response body, terminated by OK
  response.clear();
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < 5000) {
    std::string line = UartDrv::readLineSim(500);
    if (line.empty()) continue;
    log.appendf("[SIM] <%s\n", line.c_str());
    if (line.find("OK") != std::string::npos) break;
    if (line.find("ERROR") != std::string::npos) break;
    if (!response.empty()) response += '\n';
    response += line;
  }

  (void)sendAtOk("AT+HTTPTERM", 1000, log);
  return !response.empty();
}

void Sim4GModule::powerOff(LogBuffer& log) {
  if (active_) {
    (void)sendAtOk("AT+HTTPTERM", 1000, log);
    (void)sendAtOk("AT+CGACT=0,1", 3000, log);
  }

  log.appendf("[SIM] power idle HIGH\n");
  IoController::instance().setSimPower(true);
  active_ = false;
}
