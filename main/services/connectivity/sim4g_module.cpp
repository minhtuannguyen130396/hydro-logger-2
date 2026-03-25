#include "services/connectivity/sim4g_module.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "esp_log.h"
#include "board/uart_drv.hpp"
#include "common/config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modules/io/io_controller.hpp"

static const char* TAG = "SIM";

// ============================================================
// Anonymous helpers
// ============================================================
namespace {

// Remaining milliseconds until a tick deadline. Returns 0 if past.
uint32_t msRemaining(TickType_t deadline) {
  TickType_t now = xTaskGetTickCount();
  return (now < deadline) ? pdTICKS_TO_MS(deadline - now) : 0;
}

// Clamp a per-command timeout so it never exceeds the remaining budget.
uint32_t clampTimeout(TickType_t deadline, uint32_t defaultMs) {
  uint32_t rem = msRemaining(deadline);
  return std::min(rem, defaultMs);
}

// Detect real AT error responses (not URCs that happen to contain "ERROR").
bool isAtError(const std::string& s) {
  if (s.find("+CME ERROR") != std::string::npos) return true;
  if (s.find("+CMS ERROR") != std::string::npos) return true;
  // Standalone "ERROR" — check it is not embedded in another word
  size_t pos = 0;
  while ((pos = s.find("ERROR", pos)) != std::string::npos) {
    bool front_ok = (pos == 0 || !std::isalpha((unsigned char)s[pos - 1]));
    bool back_ok  = (pos + 5 >= s.size() || !std::isalpha((unsigned char)s[pos + 5]));
    if (front_ok && back_ok) return true;
    ++pos;
  }
  return false;
}

// Parse +CEREG: <n>,<stat>  or  +CREG: <n>,<stat>
// stat: 0=not registered, 1=home, 2=searching, 3=denied, 5=roaming
bool parseCeregStat(const std::string& data, int& stat) {
  const char* p = strstr(data.c_str(), "+CEREG:");
  if (!p) p = strstr(data.c_str(), "+CREG:");
  if (!p) return false;
  p = strchr(p, ':');
  if (!p) return false;
  int n = 0, s = 0;
  if (std::sscanf(p + 1, " %d,%d", &n, &s) >= 2) {
    stat = s;
    return true;
  }
  return false;
}

const char* regStatStr(int stat) {
  switch (stat) {
    case 0:  return "not_registered";
    case 1:  return "HOME";
    case 2:  return "searching";
    case 3:  return "DENIED";
    case 5:  return "ROAMING";
    default: return "unknown";
  }
}

// Parse +CSQ: <rssi>,<ber>
bool parseCsqRssi(const std::string& data, int& rssi) {
  const char* p = strstr(data.c_str(), "+CSQ:");
  if (!p) return false;
  return std::sscanf(p + 5, " %d", &rssi) == 1;
}

// HTTP helpers (unchanged from original)
struct HttpActionResult {
  int method{0};
  int status{0};
  int body_len{0};
};

bool is2xx(int status) { return status >= 200 && status < 300; }

// Delay (ms) after HTTPINIT before sending HTTPPARA, some modems need settling time.
constexpr uint32_t kHttpInitSettleMs = 300;

bool parseHttpActionLine(const std::string& line, HttpActionResult& out) {
  // Find "+HTTPACTION:" anywhere in the string — the RX chunk from readLineSim
  // may contain leading \r\n or other data before the actual URC.
  const char* p = strstr(line.c_str(), "+HTTPACTION:");
  if (!p) return false;

  int m = 0, s = 0, b = 0;
  if (std::sscanf(p, "+HTTPACTION: %d,%d,%d", &m, &s, &b) == 3 ||
      std::sscanf(p, "+HTTPACTION:%d,%d,%d", &m, &s, &b) == 3) {
    out = {m, s, b};
    return true;
  }
  return false;
}

// Vietnam MCC/MNC table for carrier detection via AT+CIMI
// CIMI returns IMSI string: first 5 digits = MCC(3) + MNC(2)
struct MccMncEntry {
  const char* prefix;   // "452XX" — first 5 digits of IMSI
  const char* carrier;  // human-readable name
  SimApnProfile apn;
};

// Sorted by most common carriers in Vietnam
constexpr MccMncEntry kVnCarriers[] = {
    {"45204", "Viettel",   SimApnProfile::Viettel},
    {"45206", "Viettel",   SimApnProfile::Viettel},    // Viettel alternate MNC
    {"45202", "VinaPhone", SimApnProfile::Vinaphone},
    {"45201", "MobiFone",  SimApnProfile::Vinaphone},  // MobiFone → use VinaPhone APN as closest match
};
constexpr int kVnCarrierCount = sizeof(kVnCarriers) / sizeof(kVnCarriers[0]);

} // namespace

// ============================================================
// APN helpers
// ============================================================
const char* Sim4GModule::apnName(SimApnProfile p) const {
  return (p == SimApnProfile::Viettel) ? "Viettel" : "VinaPhone";
}
const char* Sim4GModule::apnString(SimApnProfile p) const {
  return (p == SimApnProfile::Viettel) ? cfg::kSimApnViettel : cfg::kSimApnVinaphone;
}
SimApnProfile Sim4GModule::otherApn(SimApnProfile p) const {
  return (p == SimApnProfile::Viettel) ? SimApnProfile::Vinaphone : SimApnProfile::Viettel;
}

// Try matching a 5-digit MCC/MNC string against the Vietnam carrier table.
// Returns true and sets `out` if matched.
static bool matchMccMnc(const char* digits5, SimApnProfile& out, const char*& carrierName) {
  for (int i = 0; i < kVnCarrierCount; ++i) {
    if (std::strncmp(digits5, kVnCarriers[i].prefix, 5) == 0) {
      out = kVnCarriers[i].apn;
      carrierName = kVnCarriers[i].carrier;
      return true;
    }
  }
  return false;
}

// Extract MCC/MNC from AT+COPS? response.
// Format: +COPS: <mode>,<format>,"<oper>",<AcT>
// When format=2 (numeric), oper is MCC+MNC string like "45202".
// Some modems return format=0 (long name) — in that case oper won't be numeric.
static bool parseCopsMccMnc(const std::string& copsResponse, std::string& mccmnc) {
  // Find the quoted operator string
  size_t q1 = copsResponse.find('"');
  if (q1 == std::string::npos) return false;
  size_t q2 = copsResponse.find('"', q1 + 1);
  if (q2 == std::string::npos || q2 <= q1 + 1) return false;

  std::string oper = copsResponse.substr(q1 + 1, q2 - q1 - 1);

  // Check if it's numeric (MCC/MNC) — at least 5 digits
  if (oper.size() >= 5 && std::isdigit((unsigned char)oper[0])) {
    mccmnc = oper;
    return true;
  }
  return false;
}

// Detect SIM carrier via AT+CIMI (IMSI), fallback to AT+COPS? (numeric MCC/MNC).
// Returns the matching SimApnProfile, or the NVS-saved default on failure.
SimApnProfile Sim4GModule::detectCarrier(LogBuffer& log) {
  const SimApnProfile fallback = NvsStore::getLastSimApn();
  ESP_LOGI(TAG, "[CARRIER] detecting carrier (fallback=%s) ...", apnName(fallback));
  log.appendf("[SIM] carrier detect start (fallback=%s)\n", apnName(fallback));

  SimApnProfile detected = fallback;
  const char* carrierName = nullptr;

  // ---- Method 1: AT+CIMI (IMSI) ----
  std::string imsiLine;
  UartDrv::flushSim();
  log.appendf("[SIM] AT>AT+CIMI\n");
  if (UartDrv::writeLineSim("AT+CIMI")) {
    uint32_t start = (uint32_t)xTaskGetTickCount();
    bool found_imsi = false;
    while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < 3000) {
      std::string line = UartDrv::readLineSim(300);
      if (line.empty()) continue;
      log.appendf("[SIM] <%s\n", line.c_str());

      // IMSI is a pure digit string (15 digits typically)
      if (!found_imsi && line.size() >= 5 && std::isdigit((unsigned char)line[0])) {
        imsiLine = line;
        found_imsi = true;
      }
      if (line.find("OK") != std::string::npos) break;
      if (isAtError(line)) break;
    }

    if (found_imsi && imsiLine.size() >= 5) {
      log.appendf("[SIM] IMSI: %s\n", imsiLine.c_str());
      if (matchMccMnc(imsiLine.c_str(), detected, carrierName)) {
        ESP_LOGI(TAG, "[CARRIER] carrier=%s APN=%s (via CIMI)", carrierName, apnString(detected));
        log.appendf("[SIM] carrier=%s (via CIMI) -> APN=%s (%s)\n",
                    carrierName, apnName(detected), apnString(detected));
        return detected;
      }
    } else {
      log.appendf("[SIM] CIMI: no IMSI returned\n");
    }
  }

  // ---- Method 2: AT+COPS? (numeric format) ----
  std::string copsLine;
  if (sendAtExpect("AT+COPS?", "+COPS:", 3000, log, &copsLine)) {
    log.appendf("[SIM] COPS: %s\n", copsLine.c_str());

    std::string mccmnc;
    if (parseCopsMccMnc(copsLine, mccmnc)) {
      log.appendf("[SIM] COPS MCC/MNC: %s\n", mccmnc.c_str());
      if (matchMccMnc(mccmnc.c_str(), detected, carrierName)) {
        ESP_LOGI(TAG, "[CARRIER] carrier=%s APN=%s (via COPS)", carrierName, apnString(detected));
        log.appendf("[SIM] carrier=%s (via COPS) -> APN=%s (%s)\n",
                    carrierName, apnName(detected), apnString(detected));
        return detected;
      }
    }
  }

  // ---- Both methods failed → use NVS fallback ----
  ESP_LOGW(TAG, "[CARRIER] detect FAIL, fallback=%s", apnName(fallback));
  log.appendf("[SIM] carrier detect FAIL, fallback=%s\n", apnName(fallback));
  return fallback;
}

// ============================================================
// Low-level AT communication
// ============================================================
bool Sim4GModule::waitForToken(const char* expect, uint32_t timeoutMs,
                               LogBuffer& log, std::string* matched) {
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    std::string line = UartDrv::readLineSim(200);
    if (line.empty()) continue;

    log.appendf("[SIM] <%s\n", line.c_str());

    if (line.find(expect) != std::string::npos) {
      if (matched) *matched = line;
      return true;
    }
    if (isAtError(line)) {
      ESP_LOGE(TAG, "AT error while waiting '%s': %s", expect, line.c_str());
      log.appendf("[SIM] AT ERROR: %s\n", line.c_str());
      return false;
    }
  }
  ESP_LOGW(TAG, "timeout waiting '%s' (%lu ms)", expect, (unsigned long)timeoutMs);
  log.appendf("[SIM] timeout waiting %s\n", expect);
  return false;
}

bool Sim4GModule::sendAtExpect(const char* cmd, const char* expect,
                               uint32_t timeoutMs, LogBuffer& log,
                               std::string* matched) {
  UartDrv::flushSim();
  log.appendf("[SIM] AT>%s\n", cmd);
  if (!UartDrv::writeLineSim(cmd)) {
    ESP_LOGE(TAG, "uart write FAIL: %s", cmd);
    log.appendf("[SIM] uart write FAIL\n");
    return false;
  }
  return waitForToken(expect, timeoutMs, log, matched);
}

bool Sim4GModule::sendAtOk(const char* cmd, uint32_t timeoutMs, LogBuffer& log) {
  return sendAtExpect(cmd, "OK", timeoutMs, log);
}

// ============================================================
// PHASE 1 — Power on & AT handshake
// ============================================================
void Sim4GModule::powerOnHardware(LogBuffer& log) {
  ESP_LOGI(TAG, "[P1] power ON: GPIO LOW->HIGH edge");
  log.appendf("[SIM] power seq LOW->HIGH\n");

  IoController::instance().setSimPower(false);
  vTaskDelay(pdMS_TO_TICKS(cfg::kSimPowerEdgeDelayMs));
  IoController::instance().setSimPower(true);

  ESP_LOGI(TAG, "[P1] waiting fixed boot %d ms ...", (int)cfg::kSimBootDelayMs);
  log.appendf("[SIM] wait boot %dms\n", (int)cfg::kSimBootDelayMs);
  vTaskDelay(pdMS_TO_TICKS(cfg::kSimBootDelayMs));
}

bool Sim4GModule::waitForAtReady(uint32_t timeoutMs, LogBuffer& log) {
  static bool uart_inited = false;
  if (!uart_inited) {
    uart_inited = UartDrv::initSimUart();
  }
  if (!uart_inited) {
    ESP_LOGE(TAG, "[P1] UART init FAIL");
    log.appendf("[SIM] uart init FAIL\n");
    return false;
  }

  ESP_LOGI(TAG, "[P1] AT handshake start (timeout %lu ms)", (unsigned long)timeoutMs);
  log.appendf("[SIM] AT handshake start\n");

  const uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    if (sendAtOk("AT", 1000, log)) {
      ESP_LOGI(TAG, "[P1] AT ready in %d ms",
               (int)pdTICKS_TO_MS(xTaskGetTickCount() - start));
      log.appendf("[SIM] AT ready\n");
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(cfg::kSimAtRetryDelayMs));
  }

  ESP_LOGW(TAG, "[P1] AT handshake FAIL after %lu ms", (unsigned long)timeoutMs);
  log.appendf("[SIM] AT handshake FAIL\n");
  return false;
}

bool Sim4GModule::powerOn(LogBuffer& log) {
  // Step 1: Hardware power sequence + fixed 12s boot
  powerOnHardware(log);

  // Step 2: AT handshake (poll AT until OK)
  if (!waitForAtReady(cfg::kSimAtHandshakeTimeoutMs, log)) {
    ESP_LOGW(TAG, "[P1] power ON FAIL -> idle");
    IoController::instance().setSimPower(false);
    active_ = false;
    return false;
  }

  // Step 3: Configure LTE mode (non-fatal)
  configureLteMode(log);

  active_ = true;
  ESP_LOGI(TAG, "[P1] power ON complete, module ready for network");
  return true;
}

// ============================================================
// PHASE 2 — LTE mode configuration
// ============================================================
bool Sim4GModule::configureLteMode(LogBuffer& log) {
  ESP_LOGI(TAG, "[P2] configure LTE mode");
  log.appendf("[SIM] configure LTE mode\n");

  // Disable echo
  sendAtOk("ATE0", 1000, log);

  // Enable verbose error reporting for better debug
  sendAtOk("AT+CMEE=2", 1000, log);

  // Force LTE-only mode (AT+CNMP=38)
  // 2=Auto, 13=GSM only, 38=LTE only, 51=GSM+LTE
  if (sendAtOk("AT+CNMP=38", 3000, log)) {
    ESP_LOGI(TAG, "[P2] LTE-only mode set OK");
    log.appendf("[SIM] LTE-only mode OK\n");
  } else {
    // Non-fatal: module may not support CNMP, continue with default
    ESP_LOGW(TAG, "[P2] AT+CNMP=38 not supported, using default mode");
    log.appendf("[SIM] CNMP not supported, default mode\n");
  }

  return true;
}

// ============================================================
// PHASE 3 — Network readiness (deadline-based)
// ============================================================

// --- 3a: SIM card ready ---
bool Sim4GModule::checkSimReady(TickType_t deadline, LogBuffer& log) {
  ESP_LOGI(TAG, "[P3:SIM_READY] checking SIM card ...");
  log.appendf("[SIM] phase: SIM_READY\n");

  uint32_t tmo = clampTimeout(deadline, 5000);
  if (tmo == 0) {
    ESP_LOGW(TAG, "[P3:SIM_READY] FAIL: no time remaining");
    return false;
  }

  if (!sendAtExpect("AT+CPIN?", "READY", tmo, log)) {
    ESP_LOGE(TAG, "[P3:SIM_READY] FAIL: SIM not ready or not inserted");
    log.appendf("[SIM] FAIL at phase SIM_READY\n");
    return false;
  }

  ESP_LOGI(TAG, "[P3:SIM_READY] OK");
  return true;
}

// --- 3b: LTE registration (poll CEREG) ---
bool Sim4GModule::waitLteRegistration(TickType_t deadline, LogBuffer& log) {
  ESP_LOGI(TAG, "[P3:LTE_REG] waiting for network registration ...");
  log.appendf("[SIM] phase: LTE_REGISTER\n");

  while (msRemaining(deadline) > 0) {
    std::string matched;
    uint32_t tmo = clampTimeout(deadline, 3000);
    if (tmo == 0) break;

    if (sendAtExpect("AT+CEREG?", "+CEREG:", tmo, log, &matched)) {
      int stat = -1;
      if (parseCeregStat(matched, stat)) {
        log.appendf("[SIM] CEREG stat=%d (%s)\n", stat, regStatStr(stat));

        if (stat == 1 || stat == 5) {
          // Registered home or roaming -> log signal quality and proceed
          std::string csqLine;
          if (sendAtExpect("AT+CSQ", "+CSQ:", 2000, log, &csqLine)) {
            int rssi = 99;
            if (parseCsqRssi(csqLine, rssi)) {
              int dbm = (rssi == 99) ? 0 : (-113 + rssi * 2);
              log.appendf("[SIM] signal rssi=%d (%ddBm)\n", rssi, dbm);
            }
          }
          ESP_LOGI(TAG, "[P3:LTE_REG] OK (%s)", regStatStr(stat));
          return true;
        }

        if (stat == 3) {
          ESP_LOGE(TAG, "[P3:LTE_REG] DENIED");
          log.appendf("[SIM] FAIL at phase LTE_REGISTER: denied\n");
          return false;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(cfg::kSimRegPollIntervalMs));
  }

  ESP_LOGW(TAG, "[P3:LTE_REG] FAIL: timeout waiting for registration");
  log.appendf("[SIM] FAIL at phase LTE_REGISTER: timeout\n");
  return false;
}

// --- 3c: Configure APN ---
bool Sim4GModule::configureApnPhase(SimApnProfile profile,
                                     TickType_t deadline, LogBuffer& log) {
  ESP_LOGI(TAG, "[P3:APN] configure APN=%s (%s)", apnName(profile), apnString(profile));
  log.appendf("[SIM] phase: APN_CONFIG (%s)\n", apnName(profile));

  char cmd[96];
  std::snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apnString(profile));
  if (!sendAtOk(cmd, clampTimeout(deadline, 3000), log)) {
    ESP_LOGW(TAG, "[P3:APN] CGDCONT FAIL");
    log.appendf("[SIM] FAIL at phase APN_CONFIG: CGDCONT\n");
    return false;
  }

  if (!sendAtOk("AT+CGAUTH=1,0", clampTimeout(deadline, 2000), log)) {
    ESP_LOGW(TAG, "[P3:APN] CGAUTH FAIL");
    log.appendf("[SIM] FAIL at phase APN_CONFIG: CGAUTH\n");
    return false;
  }

  ESP_LOGI(TAG, "[P3:APN] OK");
  return true;
}

// --- 3d: Attach PS + activate PDP ---
bool Sim4GModule::attachAndActivateData(TickType_t deadline, LogBuffer& log) {
  ESP_LOGI(TAG, "[P3:ATTACH] packet service attach + PDP activate ...");
  log.appendf("[SIM] phase: ATTACH+PDP\n");

  // Request PS attach
  sendAtOk("AT+CGATT=1", clampTimeout(deadline, 5000), log);

  // Poll AT+CGATT? until attached
  bool attached = false;
  while (msRemaining(deadline) > 0) {
    uint32_t tmo = clampTimeout(deadline, 2000);
    if (tmo == 0) break;

    std::string matched;
    if (sendAtExpect("AT+CGATT?", "+CGATT:", tmo, log, &matched)) {
      if (matched.find("+CGATT: 1") != std::string::npos) {
        attached = true;
        break;
      }
      log.appendf("[SIM] CGATT: not yet, polling ...\n");
    }
    vTaskDelay(pdMS_TO_TICKS(cfg::kSimRegPollIntervalMs));
  }

  if (!attached) {
    ESP_LOGW(TAG, "[P3:ATTACH] FAIL: PS attach timeout");
    log.appendf("[SIM] FAIL at phase ATTACH: timeout\n");
    return false;
  }
  log.appendf("[SIM] PS attached OK\n");

  // Activate PDP context
  if (!sendAtOk("AT+CGACT=1,1", clampTimeout(deadline, 5000), log)) {
    ESP_LOGW(TAG, "[P3:ATTACH] FAIL: PDP activate");
    log.appendf("[SIM] FAIL at phase PDP_ACTIVATE\n");
    return false;
  }

  log.appendf("[SIM] PDP activated OK\n");
  return true;
}

// --- 3e: Verify data is actually usable ---
bool Sim4GModule::verifyDataReady(TickType_t deadline, LogBuffer& log) {
  ESP_LOGI(TAG, "[P3:VERIFY] checking data context ...");
  log.appendf("[SIM] phase: VERIFY_DATA\n");

  // Verify PDP context is active
  uint32_t tmo = clampTimeout(deadline, 3000);
  if (tmo == 0) {
    ESP_LOGW(TAG, "[P3:VERIFY] FAIL: no time remaining");
    return false;
  }

  if (!sendAtExpect("AT+CGACT?", "+CGACT: 1,1", tmo, log)) {
    ESP_LOGW(TAG, "[P3:VERIFY] FAIL: PDP context not active");
    log.appendf("[SIM] FAIL at phase VERIFY_DATA: CGACT\n");
    return false;
  }

  // Verify IP address assigned
  std::string addrLine;
  tmo = clampTimeout(deadline, 3000);
  if (sendAtExpect("AT+CGPADDR=1", "+CGPADDR:", tmo, log, &addrLine)) {
    // Check it's not 0.0.0.0
    if (addrLine.find("0.0.0.0") != std::string::npos) {
      ESP_LOGW(TAG, "[P3:VERIFY] FAIL: IP is 0.0.0.0");
      log.appendf("[SIM] FAIL at phase VERIFY_DATA: no IP\n");
      return false;
    }
    log.appendf("[SIM] IP: %s\n", addrLine.c_str());
  }

  ESP_LOGI(TAG, "[P3:VERIFY] data ready OK");
  return true;
}

// --- tryApnConnect: APN config + attach + verify (one attempt) ---
bool Sim4GModule::tryApnConnect(SimApnProfile profile,
                                 TickType_t deadline, LogBuffer& log) {
  if (msRemaining(deadline) == 0) return false;

  if (!configureApnPhase(profile, deadline, log)) return false;
  if (!attachAndActivateData(deadline, log))      return false;
  if (!verifyDataReady(deadline, log))             return false;

  return true;
}

// --- checkInternet: orchestrate all phase 3 sub-phases ---
bool Sim4GModule::checkInternet(uint32_t timeoutMs, LogBuffer& log) {
  const uint32_t budget = std::min(timeoutMs, cfg::kSimNetworkTimeoutMs);
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(budget);

  ESP_LOGI(TAG, "=== checkInternet START (budget %lu ms) ===", (unsigned long)budget);
  log.appendf("[SIM] checkInternet budget=%lums\n", (unsigned long)budget);

  if (!active_) {
    ESP_LOGW(TAG, "checkInternet: module not active (powerOn not called?)");
    log.appendf("[SIM] module not active\n");
    return false;
  }

  // Phase 3a: SIM ready
  if (!checkSimReady(deadline, log)) return false;

  // Phase 3b: LTE registration
  if (!waitLteRegistration(deadline, log)) return false;

  // Auto-detect carrier from SIM IMSI → select primary APN
  const SimApnProfile first  = detectCarrier(log);
  const SimApnProfile second = otherApn(first);

  ESP_LOGI(TAG, "[P3] primary APN=%s (fallback=%s)", apnName(first), apnName(second));
  if (tryApnConnect(first, deadline, log)) {
    active_apn_ = first;
    NvsStore::setLastSimApn(active_apn_);
    ESP_LOGI(TAG, "=== checkInternet OK (APN=%s, %lu ms remaining) ===",
             apnName(first), (unsigned long)msRemaining(deadline));
    log.appendf("[SIM] network ready APN=%s\n", apnName(first));
    return true;
  }

  // Cleanup PDP before fallback
  ESP_LOGW(TAG, "[P3] APN %s FAIL -> fallback to %s", apnName(first), apnName(second));
  log.appendf("[SIM] APN %s FAIL -> fallback %s\n", apnName(first), apnName(second));
  sendAtOk("AT+CGACT=0,1", clampTimeout(deadline, 2000), log);

  if (msRemaining(deadline) > 0 && tryApnConnect(second, deadline, log)) {
    active_apn_ = second;
    NvsStore::setLastSimApn(active_apn_);
    ESP_LOGI(TAG, "=== checkInternet OK (APN=%s, %lu ms remaining) ===",
             apnName(second), (unsigned long)msRemaining(deadline));
    log.appendf("[SIM] network ready APN=%s\n", apnName(second));
    return true;
  }

  ESP_LOGE(TAG, "=== checkInternet FAIL (both APNs, budget exhausted) ===");
  log.appendf("[SIM] checkInternet FAIL: both APNs failed\n");
  return false;
}

// ============================================================
// HTTP session helpers
// ============================================================

// Safe HTTP cleanup: only sends HTTPTERM if a session was opened.
// Never treats HTTPTERM failure as fatal.
void Sim4GModule::httpTermSafe(LogBuffer& log) {
  if (!http_inited_) return;

  UartDrv::flushSim();
  log.appendf("[SIM] AT>AT+HTTPTERM (cleanup)\n");
  UartDrv::writeLineSim("AT+HTTPTERM");

  // Best-effort: read response but ignore result
  std::string line = UartDrv::readLineSim(1000);
  if (!line.empty()) {
    log.appendf("[SIM] <%s\n", line.c_str());
  }
  http_inited_ = false;
}

// Verify PDP context 1 is active; re-activate if needed.
// Logs CGACT and CGPADDR state for debugging.
bool Sim4GModule::ensurePdpActive(LogBuffer& log) {
  // Check current PDP state
  std::string cgactLine;
  if (sendAtExpect("AT+CGACT?", "+CGACT:", 3000, log, &cgactLine)) {
    log.appendf("[SIM] PDP state: %s\n", cgactLine.c_str());

    if (cgactLine.find("+CGACT: 1,1") != std::string::npos) {
      std::string addrLine;
      if (sendAtExpect("AT+CGPADDR=1", "+CGPADDR:", 2000, log, &addrLine)) {
        log.appendf("[SIM] PDP IP: %s\n", addrLine.c_str());
      }
      return true;
    }
  }

  // PDP not active — try to re-activate
  ESP_LOGW(TAG, "[HTTP] PDP not active, re-activating");
  log.appendf("[SIM] PDP 1 not active, re-activating\n");

  if (!sendAtOk("AT+CGACT=1,1", 5000, log)) {
    ESP_LOGE(TAG, "[HTTP] PDP re-activate FAIL");
    log.appendf("[SIM] PDP re-activate FAIL\n");
    return false;
  }

  std::string addrLine;
  if (sendAtExpect("AT+CGPADDR=1", "+CGPADDR:", 2000, log, &addrLine)) {
    log.appendf("[SIM] PDP re-activated IP: %s\n", addrLine.c_str());
  }

  return true;
}

// Cleanup stale session + verify PDP + HTTPINIT for A76XX.
// A76XX does NOT use HTTPPARA="CID" — the default PDP context is used automatically.
// `method` is "GET" or "POST" for log tagging.
bool Sim4GModule::httpInitAndSetBearer(const char* method, LogBuffer& log) {
  // Cleanup any stale session
  httpTermSafe(log);

  // Verify data layer before starting HTTP
  if (!ensurePdpActive(log)) {
    ESP_LOGE(TAG, "[HTTP:%s] FAIL: PDP context dead, cannot do HTTP", method);
    log.appendf("[SIM] HTTP %s FAIL: PDP dead\n", method);
    return false;
  }

  // HTTPINIT
  ESP_LOGI(TAG, "[HTTP:%s] HTTPINIT", method);
  if (!sendAtOk("AT+HTTPINIT", 2000, log)) {
    ESP_LOGE(TAG, "[HTTP:%s] FAIL at HTTPINIT", method);
    log.appendf("[SIM] HTTP %s FAIL at HTTPINIT\n", method);
    return false;
  }
  http_inited_ = true;

  // Short settle delay — A76XX needs time after HTTPINIT
  vTaskDelay(pdMS_TO_TICKS(kHttpInitSettleMs));

  return true;
}

// ============================================================
// HTTP operations (sendPayload / httpGet)
// ============================================================
bool Sim4GModule::sendPayload(const std::string& url, const std::string& json,
                               LogBuffer& log) {
  ESP_LOGI(TAG, "[HTTP:POST] url=%s bytes=%d", url.c_str(), (int)json.size());
  log.appendf("[SIM] HTTP POST url=%s bytes=%d\n", url.c_str(), (int)json.size());

  if (!active_) {
    ESP_LOGW(TAG, "[HTTP:POST] FAIL: module inactive");
    log.appendf("[SIM] HTTP POST FAIL: module inactive\n");
    return false;
  }

  std::string actionLine;
  HttpActionResult result{};
  char cmd[320];

  // --- Step 1-2: Init HTTP + set bearer CID (with PDP verify & retry) ---
  if (!httpInitAndSetBearer("POST", log)) {
    return false;
  }

  // --- Step 3: Set URL ---
  ESP_LOGI(TAG, "[HTTP:POST] step 3/7: HTTPPARA URL");
  std::snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url.c_str());
  if (!sendAtOk(cmd, 2000, log)) {
    ESP_LOGE(TAG, "[HTTP:POST] FAIL at step HTTPPARA URL");
    log.appendf("[SIM] HTTP POST FAIL at HTTPPARA URL\n");
    httpTermSafe(log);
    return false;
  }

  // --- Step 4: Set Content-Type ---
  ESP_LOGI(TAG, "[HTTP:POST] step 4/7: HTTPPARA CONTENT");
  if (!sendAtOk("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 1000, log)) {
    ESP_LOGE(TAG, "[HTTP:POST] FAIL at step HTTPPARA CONTENT");
    log.appendf("[SIM] HTTP POST FAIL at HTTPPARA CONTENT\n");
    httpTermSafe(log);
    return false;
  }

  // --- Step 5: Upload body data ---
  ESP_LOGI(TAG, "[HTTP:POST] step 5/7: HTTPDATA (%d bytes)", (int)json.size());
  std::snprintf(cmd, sizeof(cmd), "AT+HTTPDATA=%d,%d",
                (int)json.size(), (int)cfg::kSimHttpDataTimeoutMs);
  if (!sendAtExpect(cmd, "DOWNLOAD", cfg::kSimHttpDataTimeoutMs, log)) {
    ESP_LOGE(TAG, "[HTTP:POST] FAIL at step HTTPDATA (no DOWNLOAD prompt)");
    log.appendf("[SIM] HTTP POST FAIL at HTTPDATA\n");
    httpTermSafe(log);
    return false;
  }

  log.appendf("[SIM] TX payload bytes=%d\n", (int)json.size());
  if (UartDrv::writeSim(reinterpret_cast<const uint8_t*>(json.data()),
                         (int)json.size()) != (int)json.size()) {
    ESP_LOGE(TAG, "[HTTP:POST] FAIL at step HTTPDATA (payload write)");
    log.appendf("[SIM] HTTP POST FAIL: payload write\n");
    httpTermSafe(log);
    return false;
  }
  if (!waitForToken("OK", cfg::kSimHttpDataTimeoutMs, log)) {
    ESP_LOGE(TAG, "[HTTP:POST] FAIL at step HTTPDATA (no OK after payload)");
    log.appendf("[SIM] HTTP POST FAIL: no OK after payload\n");
    httpTermSafe(log);
    return false;
  }

  // --- Step 6: Execute HTTP POST ---
  // NOTE: Do NOT wait for "OK" separately before "+HTTPACTION:".
  // A76XX can send OK and +HTTPACTION in the same UART chunk — waiting for OK
  // first would consume the +HTTPACTION URC, causing a 30s timeout.
  // waitForToken skips non-matching lines (like "OK") and catches ERROR via isAtError().
  ESP_LOGI(TAG, "[HTTP:POST] step 6/7: HTTPACTION=1");
  UartDrv::flushSim();
  log.appendf("[SIM] AT>AT+HTTPACTION=1\n");
  if (!UartDrv::writeLineSim("AT+HTTPACTION=1")) {
    ESP_LOGE(TAG, "[HTTP:POST] FAIL at step HTTPACTION (uart write)");
    log.appendf("[SIM] HTTP POST FAIL: uart write HTTPACTION\n");
    httpTermSafe(log);
    return false;
  }
  if (!waitForToken("+HTTPACTION:", cfg::kSimHttpActionTimeoutMs, log, &actionLine) ||
      !parseHttpActionLine(actionLine, result)) {
    ESP_LOGE(TAG, "[HTTP:POST] FAIL at step HTTPACTION (no +HTTPACTION response)");
    log.appendf("[SIM] HTTP POST FAIL: no +HTTPACTION URC\n");
    httpTermSafe(log);
    return false;
  }

  // --- Step 7: Check result & read response body ---
  ESP_LOGI(TAG, "[HTTP:POST] step 7/7: status=%d body=%d", result.status, result.body_len);
  log.appendf("[SIM] HTTP POST status=%d body=%d\n", result.status, result.body_len);

  // Read and log server response body (for debugging)
  if (is2xx(result.status) && result.body_len > 0) {
    char readCmd[48];
    std::snprintf(readCmd, sizeof(readCmd), "AT+HTTPREAD=0,%d", result.body_len);
    if (sendAtExpect(readCmd, "+HTTPREAD:", 5000, log)) {
      std::string body;
      uint32_t t0 = (uint32_t)xTaskGetTickCount();
      while (pdTICKS_TO_MS(xTaskGetTickCount() - t0) < 5000) {
        std::string line = UartDrv::readLineSim(500);
        if (line.empty()) continue;
        log.appendf("[SIM] <%s\n", line.c_str());
        if (line.find("OK") != std::string::npos) break;
        if (isAtError(line)) break;
        if (!body.empty()) body += '\n';
        body += line;
      }
      if (!body.empty()) {
        ESP_LOGI(TAG, "[HTTP:POST] server response: '%s'", body.c_str());
        log.appendf("[SIM] POST response: %s\n", body.c_str());
      }
    }
  }

  httpTermSafe(log);

  if (!is2xx(result.status)) {
    ESP_LOGW(TAG, "[HTTP:POST] FAIL: server returned %d", result.status);
    log.appendf("[SIM] HTTP POST FAIL: status %d\n", result.status);
    return false;
  }

  ESP_LOGI(TAG, "[HTTP:POST] OK");
  return true;
}

bool Sim4GModule::httpGet(const std::string& url, std::string& response,
                           LogBuffer& log) {
  ESP_LOGI(TAG, "[HTTP:GET] url=%s", url.c_str());
  log.appendf("[SIM] HTTP GET url=%s\n", url.c_str());

  if (!active_) {
    ESP_LOGW(TAG, "[HTTP:GET] FAIL: module inactive");
    log.appendf("[SIM] HTTP GET FAIL: module inactive\n");
    return false;
  }

  std::string actionLine;
  HttpActionResult result{};
  char cmd[320];

  // --- Step 1-2: Init HTTP + set bearer CID (with PDP verify & retry) ---
  if (!httpInitAndSetBearer("GET", log)) {
    return false;
  }

  // --- Step 3: Set URL ---
  ESP_LOGI(TAG, "[HTTP:GET] step 3/6: HTTPPARA URL");
  std::snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url.c_str());
  if (!sendAtOk(cmd, 2000, log)) {
    ESP_LOGE(TAG, "[HTTP:GET] FAIL at step HTTPPARA URL");
    log.appendf("[SIM] HTTP GET FAIL at HTTPPARA URL\n");
    httpTermSafe(log);
    return false;
  }

  // --- Step 4: Execute HTTP GET ---
  // NOTE: Do NOT wait for "OK" separately before "+HTTPACTION:".
  // A76XX can send OK and +HTTPACTION in the same UART chunk — waiting for OK
  // first would consume the +HTTPACTION URC, causing a 30s timeout.
  ESP_LOGI(TAG, "[HTTP:GET] step 4/6: HTTPACTION=0");
  UartDrv::flushSim();
  log.appendf("[SIM] AT>AT+HTTPACTION=0\n");
  if (!UartDrv::writeLineSim("AT+HTTPACTION=0")) {
    ESP_LOGE(TAG, "[HTTP:GET] FAIL at step HTTPACTION (uart write)");
    log.appendf("[SIM] HTTP GET FAIL: uart write HTTPACTION\n");
    httpTermSafe(log);
    return false;
  }
  if (!waitForToken("+HTTPACTION:", cfg::kSimHttpActionTimeoutMs, log, &actionLine) ||
      !parseHttpActionLine(actionLine, result)) {
    ESP_LOGE(TAG, "[HTTP:GET] FAIL at step HTTPACTION (no +HTTPACTION response)");
    log.appendf("[SIM] HTTP GET FAIL: no +HTTPACTION URC\n");
    httpTermSafe(log);
    return false;
  }

  ESP_LOGI(TAG, "[HTTP:GET] action result: status=%d body=%d", result.status, result.body_len);
  log.appendf("[SIM] HTTP GET status=%d body=%d\n", result.status, result.body_len);
  if (!is2xx(result.status) || result.body_len <= 0) {
    ESP_LOGW(TAG, "[HTTP:GET] FAIL: server returned %d (body=%d)",
             result.status, result.body_len);
    log.appendf("[SIM] HTTP GET FAIL: status %d\n", result.status);
    httpTermSafe(log);
    return false;
  }

  // --- Step 5: Read response body ---
  // NOTE: A76XX often sends +HTTPREAD header, body data, and OK in a single UART
  // chunk. Do NOT use waitForToken("+HTTPREAD:") separately — it would consume
  // the body. Instead, accumulate all raw data then parse out the body.
  ESP_LOGI(TAG, "[HTTP:GET] step 5/6: HTTPREAD (%d bytes)", result.body_len);
  std::snprintf(cmd, sizeof(cmd), "AT+HTTPREAD=0,%d", result.body_len);
  UartDrv::flushSim();
  log.appendf("[SIM] AT>%s\n", cmd);
  if (!UartDrv::writeLineSim(cmd)) {
    ESP_LOGE(TAG, "[HTTP:GET] FAIL at step HTTPREAD (uart write)");
    log.appendf("[SIM] HTTP GET FAIL: uart write HTTPREAD\n");
    httpTermSafe(log);
    return false;
  }

  // Accumulate all HTTPREAD response data into one buffer
  std::string rawRead;
  {
    uint32_t t0 = (uint32_t)xTaskGetTickCount();
    while (pdTICKS_TO_MS(xTaskGetTickCount() - t0) < 5000) {
      std::string chunk = UartDrv::readLineSim(500);
      if (chunk.empty()) continue;
      log.appendf("[SIM] <%s\n", chunk.c_str());
      rawRead += chunk;
      // Stop once we have +HTTPREAD header AND a trailing OK
      if (rawRead.find("+HTTPREAD:") != std::string::npos &&
          rawRead.find("OK") != std::string::npos) {
        break;
      }
      if (isAtError(rawRead)) break;
    }
  }

  // Parse body: everything between the first \n after "+HTTPREAD: <n>" and "OK"
  response.clear();
  size_t hdr = rawRead.find("+HTTPREAD:");
  if (hdr != std::string::npos) {
    size_t bodyStart = rawRead.find('\n', hdr);
    if (bodyStart != std::string::npos) {
      bodyStart++;  // skip the \n
      // Find trailing OK (search backwards for robustness)
      size_t bodyEnd = rawRead.rfind("OK");
      if (bodyEnd != std::string::npos && bodyEnd > bodyStart) {
        response = rawRead.substr(bodyStart, bodyEnd - bodyStart);
      } else {
        // No OK found — take everything after header
        response = rawRead.substr(bodyStart);
      }
      // Trim trailing \r\n whitespace
      while (!response.empty() &&
             (response.back() == '\r' || response.back() == '\n' || response.back() == ' ')) {
        response.pop_back();
      }
    }
  }

  if (!response.empty()) {
    ESP_LOGI(TAG, "[HTTP:GET] parsed body (%d bytes): '%s'",
             (int)response.size(), response.c_str());
    log.appendf("[SIM] GET body: %s\n", response.c_str());
  } else {
    ESP_LOGW(TAG, "[HTTP:GET] HTTPREAD raw: '%s'", rawRead.c_str());
    log.appendf("[SIM] HTTPREAD raw (unparsed): %s\n", rawRead.c_str());
  }

  // --- Step 6: Cleanup ---
  ESP_LOGI(TAG, "[HTTP:GET] step 6/6: cleanup");
  httpTermSafe(log);

  if (response.empty()) {
    ESP_LOGW(TAG, "[HTTP:GET] FAIL: empty response body");
    log.appendf("[SIM] HTTP GET FAIL: empty body\n");
    return false;
  }

  ESP_LOGI(TAG, "[HTTP:GET] OK response='%s'", response.c_str());
  return true;
}

// ============================================================
// Power off
// ============================================================
void Sim4GModule::powerOff(LogBuffer& log) {
  ESP_LOGI(TAG, "power OFF");
  log.appendf("[SIM] power idle LOW\n");
  IoController::instance().setSimPower(false);
  active_ = false;
  http_inited_ = false;
}
