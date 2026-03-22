#include "test_common.hpp"
#include "board/uart_drv.hpp"
#include "board/pins.hpp"
#include "modules/io/io_controller.hpp"

#include <cstdio>
#include <string>

static const char* NAME = "SIM";

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
// Reads all data until timeout, then checks the full response at once.
static bool sendAt(const char* cmd, const char* expect, uint32_t timeoutMs) {
  UartDrv::flushSim();
  TEST_INFO(NAME, "AT> %s", cmd);

  if (!UartDrv::writeLineSim(cmd)) {
    TEST_INFO(NAME, "UART write FAIL");
    return false;
  }

  std::string resp = UartDrv::readLineSim(timeoutMs);
  logRawResponse(resp);

  if (resp.empty()) {
    TEST_INFO(NAME, "  timeout - no data received");
    return false;
  }

  if (resp.find(expect) != std::string::npos) {
    return true;
  }
  if (resp.find("ERROR") != std::string::npos) {
    return false;
  }

  TEST_INFO(NAME, "  '%s' not found in response", expect);
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

  // Power on (edge trigger HIGH->LOW)
  TEST_INFO(NAME, "Power ON (HIGH -> LOW edge)");
  IoController::instance().setSimPower(true);
  testDelayMs(100);
  IoController::instance().setSimPower(false);
  TEST_INFO(NAME, "Wait boot 12000ms...");
  testDelayMs(12000);

  // AT handshake at 115200 baud: try AT + get phone number
  bool at_ok = false;
  for (int i = 0; i < 50; i++) {
    if (sendAt("AT", "OK", 3000)) { at_ok = true; break; }
    if (sendAt("AT+CNUM", "OK", 3000)) { at_ok = true; break; }
    testDelayMs(2000);
  }

  if (!at_ok) {
    TEST_FAIL(NAME, "AT handshake failed - module not responding");
    IoController::instance().setSimPower(true);
    return;
  }
  TEST_INFO(NAME, "AT handshake OK");

  // Disable echo
  sendAt("ATE0", "OK", 1000);

  // Check SIM card
  bool sim_ok = sendAt("AT+CPIN?", "READY", 2000);
  TEST_INFO(NAME, "SIM card: %s", sim_ok ? "READY" : "NOT DETECTED");

  // Check signal
  sendAt("AT+CSQ", "OK", 1000);

  // Check module info
  sendAt("AT+CGMM", "OK", 1000); // Module model
  sendAt("AT+CIMI", "OK", 1000); // IMSI

  // Try network attach
  bool net_ok = sendAt("AT+CGATT=1", "OK", 5000);
  TEST_INFO(NAME, "Network attach: %s", net_ok ? "OK" : "FAIL");

  if (net_ok) {
    // Try APN config (Viettel)
    sendAt("AT+CGDCONT=1,\"IP\",\"v-internet\"", "OK", 3000);
    sendAt("AT+CGAUTH=1,0", "OK", 2000);

    bool pdp_ok = sendAt("AT+CGACT=1,1", "OK", 5000);
    TEST_INFO(NAME, "PDP context: %s", pdp_ok ? "ACTIVE" : "FAIL");

    // Check attach status
    sendAt("AT+CGATT?", "OK", 2000);
    sendAt("AT+CGACT?", "OK", 2000);

    // Deactivate
    sendAt("AT+CGACT=0,1", "OK", 3000);
  }

  // Power off
  TEST_INFO(NAME, "Power OFF");
  IoController::instance().setSimPower(true); // idle HIGH

  if (sim_ok && net_ok) {
    TEST_PASS(NAME);
  } else if (sim_ok) {
    TEST_INFO(NAME, "SIM detected but network failed");
    TEST_FAIL(NAME, "network attach failed");
  } else {
    TEST_FAIL(NAME, "SIM card not detected");
  }
}
