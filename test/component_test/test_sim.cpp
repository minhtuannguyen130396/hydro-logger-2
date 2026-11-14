#include "test_common.hpp"
#include "board/uart_drv.hpp"
#include "board/pins.hpp"
#include "modules/io/io_controller.hpp"

#include <cstdio>
#include <cstring>
#include <string>

static const char* NAME = "SIM";
static const char* kTestHttpUrl = "http://www.google.com";

static void logRawResponse(const std::string& line) {
  char hexbuf[256];
  int pos = 0;

  for (unsigned char ch : line) {
    if (pos >= (int)sizeof(hexbuf) - 4) break;
    int written = std::snprintf(&hexbuf[pos], sizeof(hexbuf) - pos, "%02X ", ch);
    if (written <= 0) break;
    pos += written;
  }

  if (pos == 0) {
    std::snprintf(hexbuf, sizeof(hexbuf), "(empty)");
  } else if (pos > 0 && pos < (int)sizeof(hexbuf)) {
    hexbuf[pos - 1] = '\0';
  }

  TEST_INFO(NAME, "  response: '%s'", line.c_str());
  TEST_INFO(NAME, "  response(hex): %s", hexbuf);
}

// Simple AT command sender for test mode
static bool sendAt(const char* cmd, const char* expect, uint32_t timeoutMs) {
  UartDrv::flushSim();
  TEST_INFO(NAME, "AT> %s", cmd);

  if (!UartDrv::writeLineSim(cmd)) {
    TEST_INFO(NAME, "UART write FAIL");
    return false;
  }

  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    std::string line = UartDrv::readLineSim(200);
    if (line.empty()) continue;

    logRawResponse(line);

    if (line.find(expect) != std::string::npos) {
      return true;
    }
    if (line.find("ERROR") != std::string::npos) {
      return false;
    }
  }

  TEST_INFO(NAME, "  timeout waiting for '%s'", expect);
  return false;
}

// Send AT command and capture the matching line
static bool sendAtWithLine(const char* cmd, const char* expect, uint32_t timeoutMs, std::string* matched = nullptr) {
  UartDrv::flushSim();
  TEST_INFO(NAME, "AT> %s", cmd);

  if (!UartDrv::writeLineSim(cmd)) {
    TEST_INFO(NAME, "UART write FAIL");
    return false;
  }

  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    std::string line = UartDrv::readLineSim(200);
    if (line.empty()) continue;

    logRawResponse(line);

    if (line.find(expect) != std::string::npos) {
      if (matched) *matched = line;
      return true;
    }
    if (line.find("ERROR") != std::string::npos) {
      return false;
    }
  }

  TEST_INFO(NAME, "  timeout waiting for '%s'", expect);
  return false;
}

// Wait for a token and optionally return the matched line
static bool waitToken(const char* expect, uint32_t timeoutMs, std::string* matched = nullptr) {
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    std::string line = UartDrv::readLineSim(200);
    if (line.empty()) continue;

    logRawResponse(line);

    if (line.find(expect) != std::string::npos) {
      if (matched) *matched = line;
      return true;
    }
    if (line.find("ERROR") != std::string::npos) {
      return false;
    }
  }

  TEST_INFO(NAME, "  timeout waiting for '%s'", expect);
  return false;
}

// Carrier detection from IMSI prefix
static const char* detectApnFromImsi(const std::string& imsi) {
  if (imsi.find("45202") != std::string::npos) {
    return "m3-world";   // VinaPhone
  } else if (imsi.find("45204") != std::string::npos || imsi.find("45206") != std::string::npos) {
    return "v-internet"; // Viettel
  } else if (imsi.find("45201") != std::string::npos) {
    return "m3-world";   // MobiFone
  }
  return "v-internet";   // Default fallback
}

static const char* carrierNameFromImsi(const std::string& imsi) {
  if (imsi.find("45202") != std::string::npos) return "VinaPhone";
  if (imsi.find("45204") != std::string::npos || imsi.find("45206") != std::string::npos) return "Viettel";
  if (imsi.find("45201") != std::string::npos) return "MobiFone";
  return "Unknown (Default Viettel APN)";
}

// Parse network registration status: 1 = Home, 5 = Roaming
static bool parseNetworkRegistered(const std::string& line, int& outStat) {
  const char* p = std::strstr(line.c_str(), "+CEREG:");
  if (!p) p = std::strstr(line.c_str(), "+CREG:");
  if (!p) return false;
  p = std::strchr(p, ':');
  if (!p) return false;
  int n = 0, s = 0;
  if (std::sscanf(p + 1, " %d,%d", &n, &s) >= 2) {
    outStat = s;
    return (s == 1 || s == 5);
  }
  return false;
}

// Single attempt HTTP GET via modem AT commands
static bool executeHttpGetOnce(const char* url, int& outStatus, int& outBodyLen) {
  outStatus = 0;
  outBodyLen = 0;
  char cmd[320];

  // Best-effort cleanup of any lingering session
  sendAt("AT+HTTPTERM", "OK", 1000);

  // Initialize HTTP service
  if (!sendAt("AT+HTTPINIT", "OK", 3000)) {
    TEST_INFO(NAME, "HTTPINIT failed");
    return false;
  }

  // Set CID (Context Identifier) = 1 (active PDP context)
  if (!sendAt("AT+HTTPPARA=\"CID\",1", "OK", 2000)) {
    TEST_INFO(NAME, "HTTPPARA CID failed");
    sendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }

  // Set target URL
  std::snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
  if (!sendAt(cmd, "OK", 2000)) {
    TEST_INFO(NAME, "HTTPPARA URL failed");
    sendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }

  // Trigger HTTP GET (method 0)
  UartDrv::flushSim();
  TEST_INFO(NAME, "AT> AT+HTTPACTION=0");
  if (!UartDrv::writeLineSim("AT+HTTPACTION=0")) {
    TEST_INFO(NAME, "UART write HTTPACTION failed");
    sendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }

  // Wait for +HTTPACTION: <method>,<status>,<datalen>
  // On 4G, DNS lookup + TCP handshake + HTTP response can take several seconds.
  std::string actionLine;
  if (!waitToken("+HTTPACTION:", 25000, &actionLine)) {
    TEST_INFO(NAME, "Wait for +HTTPACTION: timed out or error");
    sendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }

  // Parse +HTTPACTION: 0,<status>,<bodyLen>
  int method = 0;
  const char* p = std::strstr(actionLine.c_str(), "+HTTPACTION:");
  if (p) {
    std::sscanf(p, "+HTTPACTION: %d,%d,%d", &method, &outStatus, &outBodyLen);
  }
  TEST_INFO(NAME, "HTTP result: method=%d, status=%d, bodyLen=%d", method, outStatus, outBodyLen);

  // If status is 2xx or 3xx (like 200 OK, 301/302 Redirect), read sample body
  if (outStatus >= 200 && outStatus < 400) {
    if (outBodyLen > 0) {
      int readLen = (outBodyLen > 64) ? 64 : outBodyLen;
      std::snprintf(cmd, sizeof(cmd), "AT+HTTPREAD=0,%d", readLen);
      sendAt(cmd, "OK", 3000);
    }
    sendAt("AT+HTTPTERM", "OK", 1000);
    return true;
  }

  sendAt("AT+HTTPTERM", "OK", 1000);
  return false;
}

// HTTP GET Internet confirmation with retry (max 2 attempts)
static bool testHttpGetInternet(const char* url, int maxAttempts = 2) {
  TEST_INFO(NAME, "=== HTTP GET Internet Check (URL: %s, max %d attempts) ===", url, maxAttempts);
  for (int attempt = 1; attempt <= maxAttempts; attempt++) {
    TEST_INFO(NAME, "--- Attempt %d/%d: Sending HTTP GET ---", attempt, maxAttempts);
    int status = 0, bodyLen = 0;
    if (executeHttpGetOnce(url, status, bodyLen)) {
      TEST_INFO(NAME, ">>> Attempt %d SUCCESS (HTTP status=%d, len=%d) -> Internet CONFIRMED! <<<",
                attempt, status, bodyLen);
      return true;
    }
    TEST_INFO(NAME, ">>> Attempt %d FAILED (HTTP status=%d) <<<", attempt, status);
    if (attempt < maxAttempts) {
      TEST_INFO(NAME, "Waiting 2000ms before retry...");
      testDelayMs(2000);
    }
  }
  TEST_INFO(NAME, ">>> All %d attempts failed. Internet NOT confirmed! <<<", maxAttempts);
  return false;
}

void test_sim() {
  TEST_START(NAME);

  // Init UART2 at the configured baud.
  TEST_INFO(NAME, "Init UART2 baud=%d", pins::UART_SIM_BAUD);
  if (!UartDrv::initSimUart()) {
    TEST_FAIL(NAME, "UART2 init failed");
    return;
  }

  // Power on (edge trigger LOW->HIGH)
  TEST_INFO(NAME, "Power ON (LOW -> HIGH)");
  IoController::instance().setSimPower(false);
  testDelayMs(100);
  IoController::instance().setSimPower(true);
  TEST_INFO(NAME, "Wait boot 12000ms...");
  testDelayMs(12000);

  // AT handshake at the configured baud with a longer timeout.
  bool at_ok = false;
  for (int i = 0; i < 50; i++) {
    if (sendAt("AT", "OK", 2000)) {
      at_ok = true;
      break;
    }
    testDelayMs(500);
  }

  if (!at_ok) {
    TEST_FAIL(NAME, "AT handshake failed - module not responding");
    IoController::instance().setSimPower(false);
    return;
  }
  TEST_INFO(NAME, "AT handshake OK");

  // Disable echo
  sendAt("ATE0", "OK", 1000);

  // Check SIM card
  bool sim_ok = sendAt("AT+CPIN?", "READY", 2000);
  TEST_INFO(NAME, "SIM card: %s", sim_ok ? "READY" : "NOT DETECTED");
  if (!sim_ok) {
    IoController::instance().setSimPower(false);
    TEST_FAIL(NAME, "SIM card not detected");
    return;
  }

  // Check module model
  sendAt("AT+CGMM", "OK", 1000);

  // Check IMSI & Auto detect Carrier APN
  std::string imsiLine;
  std::string imsi;
  if (sendAtWithLine("AT+CIMI", "OK", 2000, &imsiLine)) {
    imsi = imsiLine;
  }
  const char* carrier = carrierNameFromImsi(imsi);
  const char* apn = detectApnFromImsi(imsi);
  TEST_INFO(NAME, "Detected Carrier: %s (APN: '%s')", carrier, apn);

  // Wait for cellular signal & LTE network registration (AT+CEREG?) up to 40 seconds
  TEST_INFO(NAME, "Waiting for LTE registration (max 40s)...");
  bool net_registered = false;
  uint32_t waitStart = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - waitStart) < 40000) {
    // Check signal strength
    sendAt("AT+CSQ", "OK", 1000);

    // Check LTE registration status
    std::string regLine;
    int regStat = 0;
    if (sendAtWithLine("AT+CEREG?", "+CEREG:", 1500, &regLine)) {
      if (parseNetworkRegistered(regLine, regStat)) {
        TEST_INFO(NAME, "LTE Network registered OK (stat=%d)!", regStat);
        net_registered = true;
        break;
      } else {
        TEST_INFO(NAME, "  Searching network... (stat=%d: 0=not reg, 2=searching, 3=denied)", regStat);
      }
    }
    testDelayMs(2000);
  }

  if (!net_registered) {
    TEST_INFO(NAME, "LTE registration timeout - trying AT+CGATT anyway");
  }

  // Attach data service
  bool net_ok = sendAt("AT+CGATT=1", "OK", 5000);
  TEST_INFO(NAME, "Network attach (CGATT): %s", net_ok ? "OK" : "FAIL");

  bool pdp_ok = false;
  bool internet_ok = false;

  if (net_ok) {
    // Configure APN dynamically based on SIM
    char apnCmd[128];
    std::snprintf(apnCmd, sizeof(apnCmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    sendAt(apnCmd, "OK", 3000);
    sendAt("AT+CGAUTH=1,0", "OK", 2000);

    pdp_ok = sendAt("AT+CGACT=1,1", "OK", 8000);
    TEST_INFO(NAME, "PDP context: %s", pdp_ok ? "ACTIVE" : "FAIL");

    // Fallback APN retry if primary APN failed
    if (!pdp_ok) {
      const char* fallbackApn = (std::strcmp(apn, "m3-world") == 0) ? "v-internet" : "m3-world";
      TEST_INFO(NAME, "Retrying PDP activation with fallback APN: '%s'...", fallbackApn);
      std::snprintf(apnCmd, sizeof(apnCmd), "AT+CGDCONT=1,\"IP\",\"%s\"", fallbackApn);
      sendAt(apnCmd, "OK", 3000);
      pdp_ok = sendAt("AT+CGACT=1,1", "OK", 8000);
      TEST_INFO(NAME, "Fallback PDP context: %s", pdp_ok ? "ACTIVE" : "FAIL");
    }

    if (pdp_ok) {
      // Check assigned IP address
      sendAt("AT+CGPADDR=1", "OK", 2000);

      // Test HTTP GET confirm Internet (retry up to 2 attempts)
      internet_ok = testHttpGetInternet(kTestHttpUrl, 2);
    }

    // Deactivate PDP context
    sendAt("AT+CGACT=0,1", "OK", 3000);
  }

  // Power off module
  TEST_INFO(NAME, "Power OFF");
  IoController::instance().setSimPower(false); // idle LOW

  if (sim_ok && net_ok && pdp_ok && internet_ok) {
    TEST_PASS(NAME);
  } else if (sim_ok && net_ok && pdp_ok) {
    TEST_FAIL(NAME, "Internet HTTP GET confirmation failed");
  } else if (sim_ok && net_ok) {
    TEST_FAIL(NAME, "PDP context activation failed");
  } else if (sim_ok) {
    TEST_INFO(NAME, "SIM detected but network attach failed");
    TEST_FAIL(NAME, "network attach failed");
  } else {
    TEST_FAIL(NAME, "SIM card not detected");
  }
}
