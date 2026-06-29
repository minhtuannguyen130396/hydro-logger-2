#include "test_common.hpp"
#include "board/uart_drv.hpp"
#include "board/pins.hpp"
#include "modules/io/io_controller.hpp"
#include "common/config.hpp"

#include "esp_ota_ops.h"
#include "esp_partition.h"

#include <cstdio>
#include <cstring>
#include <string>

// Self-contained OTA-over-SIM test (mirrors test_post_api style: raw AT commands,
// no dependency on Sim4GModule/OtaService). Two entry points:
//   test_ota()          - fetch + parse the version endpoint only (fast)
//   test_ota_download()  - full firmware download into the *inactive* OTA
//                          partition with esp_ota_* verification. NON-DESTRUCTIVE:
//                          it never calls esp_ota_set_boot_partition and never
//                          reboots, so the running firmware is untouched. This is
//                          the real validation of the AT+HTTPREAD framing + flash
//                          write path used by Sim4GModule::downloadOtaImage().

static const char* NAME = "OTA";

static bool is2xx(int s) { return s >= 200 && s < 300; }

// ──────────────────────────────────────────────
// Basic AT helpers
// ──────────────────────────────────────────────
static bool simSendAt(const char* cmd, const char* expect, uint32_t timeoutMs) {
  UartDrv::flushSim();
  TEST_INFO(NAME, "AT> %s", cmd);
  if (!UartDrv::writeLineSim(cmd)) return false;

  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    std::string line = UartDrv::readLineSim(200);
    if (line.empty()) continue;
    TEST_INFO(NAME, "  <- %s", line.c_str());
    if (line.find(expect) != std::string::npos) return true;
    if (line.find("ERROR") != std::string::npos) return false;
  }
  TEST_INFO(NAME, "  timeout waiting '%s'", expect);
  return false;
}

static bool simWaitToken(const char* token, uint32_t timeoutMs, std::string* matched = nullptr) {
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    std::string line = UartDrv::readLineSim(500);
    if (line.empty()) continue;
    TEST_INFO(NAME, "  <- %s", line.c_str());
    if (line.find(token) != std::string::npos) {
      if (matched) *matched = line;
      return true;
    }
    if (line.find("ERROR") != std::string::npos) return false;
  }
  return false;
}

// ──────────────────────────────────────────────
// Connect / disconnect (LOW->HIGH power edge, matches Sim4GModule)
// ──────────────────────────────────────────────
static bool simConnect() {
  if (!UartDrv::initSimUart()) return false;

  TEST_INFO(NAME, "SIM power ON (LOW->HIGH), wait boot...");
  IoController::instance().setSimPower(false);
  testDelayMs(100);
  IoController::instance().setSimPower(true);
  testDelayMs(20000);

  bool at_ok = false;
  for (int i = 0; i < 30; i++) {
    if (simSendAt("AT", "OK", 1000)) { at_ok = true; break; }
    testDelayMs(500);
  }
  if (!at_ok) return false;

  simSendAt("ATE0", "OK", 1000);
  simSendAt("AT+CPIN?", "READY", 3000);
  if (!simSendAt("AT+CGATT=1", "OK", 8000)) return false;

  // Try Viettel APN, then VinaPhone (covers both common carriers).
  const char* apns[] = {cfg::kSimApnViettel, cfg::kSimApnVinaphone};
  for (const char* apn : apns) {
    char cmd[96];
    std::snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", apn);
    simSendAt(cmd, "OK", 3000);
    simSendAt("AT+CGAUTH=1,0", "OK", 2000);
    if (simSendAt("AT+CGACT=1,1", "OK", 8000)) {
      TEST_INFO(NAME, "PDP active via APN '%s'", apn);
      return true;
    }
    TEST_INFO(NAME, "APN '%s' failed, trying next", apn);
    simSendAt("AT+CGACT=0,1", "OK", 3000);
  }
  return false;
}

static void simDisconnect() {
  simSendAt("AT+HTTPTERM", "OK", 1000);
  simSendAt("AT+CGACT=0,1", "OK", 3000);
  IoController::instance().setSimPower(false);
}

// ──────────────────────────────────────────────
// HTTP GET (small text body) — used for the version endpoint
// ──────────────────────────────────────────────
static bool simHttpGet(const char* url, std::string& body) {
  char cmd[320];
  body.clear();

  simSendAt("AT+HTTPTERM", "OK", 1000);
  if (!simSendAt("AT+HTTPINIT", "OK", 2000)) return false;
  std::snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
  if (!simSendAt(cmd, "OK", 2000)) { simSendAt("AT+HTTPTERM", "OK", 1000); return false; }

  UartDrv::flushSim();
  TEST_INFO(NAME, "AT> AT+HTTPACTION=0");
  UartDrv::writeLineSim("AT+HTTPACTION=0");

  std::string actionLine;
  if (!simWaitToken("+HTTPACTION:", 30000, &actionLine)) { simSendAt("AT+HTTPTERM", "OK", 1000); return false; }

  int method = 0, status = 0, bodyLen = 0;
  const char* p = std::strstr(actionLine.c_str(), "+HTTPACTION:");
  if (p) {
    std::sscanf(p, "+HTTPACTION: %d,%d,%d", &method, &status, &bodyLen);
  }
  TEST_INFO(NAME, "GET status=%d bodyLen=%d", status, bodyLen);
  if (!is2xx(status) || bodyLen <= 0) { simSendAt("AT+HTTPTERM", "OK", 1000); return false; }

  std::snprintf(cmd, sizeof(cmd), "AT+HTTPREAD=0,%d", bodyLen);
  UartDrv::flushSim();
  TEST_INFO(NAME, "AT> %s", cmd);
  UartDrv::writeLineSim(cmd);

  std::string rawRead;
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < 5000) {
    std::string chunk = UartDrv::readLineSim(500);
    if (chunk.empty()) continue;
    TEST_INFO(NAME, "  <- %s", chunk.c_str());
    rawRead += chunk;
    if (rawRead.find("+HTTPREAD:") != std::string::npos &&
        rawRead.find("OK") != std::string::npos) {
      break;
    }
    if (rawRead.find("ERROR") != std::string::npos) break;
  }

  size_t hdr = rawRead.find("+HTTPREAD:");
  if (hdr != std::string::npos) {
    size_t bodyStart = rawRead.find('\n', hdr);
    if (bodyStart != std::string::npos) {
      bodyStart++;
      size_t bodyEnd = rawRead.rfind("OK");
      if (bodyEnd != std::string::npos && bodyEnd > bodyStart) {
        body = rawRead.substr(bodyStart, bodyEnd - bodyStart);
      } else {
        body = rawRead.substr(bodyStart);
      }
      while (!body.empty() && (body.back() == '\r' || body.back() == '\n' || body.back() == ' ')) {
        body.pop_back();
      }
    }
  }
  simSendAt("AT+HTTPTERM", "OK", 1000);
  return !body.empty();
}

// Extract "fw_version" (or "version") string value from a small JSON body.
static bool parseFwVersion(const std::string& body, std::string& out) {
  size_t key = body.find("fw_version");
  if (key == std::string::npos) key = body.find("\"version\"");
  if (key == std::string::npos) return false;
  size_t colon = body.find(':', key);
  if (colon == std::string::npos) return false;
  size_t q1 = body.find('"', colon);
  if (q1 == std::string::npos) return false;
  size_t q2 = body.find('"', q1 + 1);
  if (q2 == std::string::npos) return false;
  out = body.substr(q1 + 1, q2 - q1 - 1);
  return !out.empty();
}

// ──────────────────────────────────────────────
// Binary-safe HTTPREAD helpers (mirror Sim4GModule::downloadOtaImage)
// ──────────────────────────────────────────────
// Diagnostic return codes for the HTTPREAD header read:
//    1 = header parsed, len>0 (set in `len`)
//    0 = "+HTTPREAD: 0" — modem reports no more data (cache/connection exhausted)
//   -1 = AT "ERROR" line
//   -2 = timeout: no recognizable response within timeoutMs
// `detail` carries the offending / last line so the trace can tell the cases apart.
static int readHttpReadLen(int& len, uint32_t timeoutMs, std::string& detail) {
  std::string line, lastSeen;
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    uint8_t c;
    if (UartDrv::readSim(&c, 1, 200) != 1) continue;
    if (c == '\n') {
      const char* p = strstr(line.c_str(), "+HTTPREAD:");
      if (p) {
        int n = 0;
        if (std::sscanf(p + 10, " %d", &n) == 1) { len = n; detail = line; return n > 0 ? 1 : 0; }
      }
      if (line.find("ERROR") != std::string::npos) { detail = line; return -1; }
      if (!line.empty()) lastSeen = line;
      line.clear();
    } else if (c != '\r') {
      line.push_back((char)c);
    }
  }
  detail = !line.empty() ? line : lastSeen;
  return -2;
}

static void drainToOk(uint32_t timeoutMs) {
  std::string s;
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs) {
    uint8_t c;
    if (UartDrv::readSim(&c, 1, 100) != 1) continue;
    s.push_back((char)c);
    if (s.size() > 64) s.erase(0, s.size() - 64);
    if (s.find("OK\r") != std::string::npos || s.find("OK\n") != std::string::npos) return;
  }
}

// Log signal strength + network type once. A ~0.6 KB/s download points at a weak
// link (2G fallback / poor RSSI) rather than the AT read path, so capture it.
static void logSignalInfo() {
  simSendAt("AT+CSQ", "OK", 2000);    // +CSQ: <rssi>,<ber>   (rssi 99 = unknown)
  simSendAt("AT+CPSI?", "OK", 3000);  // +CPSI: <netType>,...,<dBm>  (SIMCom)
}

// Download `url` into the inactive OTA partition and verify it, WITHOUT switching
// the boot partition. Returns true if the full image was written and esp_ota_end
// (image hash verification) passed.
//
// Uses HTTP Range requests: the file is fetched in small windows, each its own
// GET carrying a "Range: bytes=A-B" header (AT+HTTPPARA="USERDATA"). This keeps
// every modem body buffer well under the ~149 KB cap that made a single whole-file
// AT+HTTPACTION + ranged AT+HTTPREAD fail with ERROR at offset 152576, and keeps
// each request short so the server's 5 s keep-alive idle timeout never trips.
static bool simHttpDownloadVerify(const char* url) {
  char cmd[360];

  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  if (!part) { TEST_INFO(NAME, "no OTA partition"); return false; }
  TEST_INFO(NAME, "target partition=%s size=%uK", part->label, (unsigned)(part->size / 1024));

  logSignalInfo();

  // --- Phase 0: probe total size with one (non-ranged) HTTPACTION header ---
  simSendAt("AT+HTTPTERM", "OK", 1000);
  if (!simSendAt("AT+HTTPINIT", "OK", 2000)) return false;
  std::snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
  if (!simSendAt(cmd, "OK", 2000)) { simSendAt("AT+HTTPTERM", "OK", 1000); return false; }

  UartDrv::flushSim();
  TEST_INFO(NAME, "AT> AT+HTTPACTION=0 (size probe)");
  UartDrv::writeLineSim("AT+HTTPACTION=0");
  std::string actionLine;
  if (!simWaitToken("+HTTPACTION:", 30000, &actionLine)) { simSendAt("AT+HTTPTERM", "OK", 1000); return false; }

  int method = 0, status = 0, total = 0;
  const char* p = std::strstr(actionLine.c_str(), "+HTTPACTION:");
  if (p) std::sscanf(p, "+HTTPACTION: %d,%d,%d", &method, &status, &total);
  TEST_INFO(NAME, "bin status=%d size=%d bytes", status, total);
  if (!is2xx(status) || total <= 0) { simSendAt("AT+HTTPTERM", "OK", 1000); return false; }
  if ((uint32_t)total > part->size) {
    TEST_INFO(NAME, "image %d > partition %u", total, (unsigned)part->size);
    simSendAt("AT+HTTPTERM", "OK", 1000);
    return false;
  }
  simSendAt("AT+HTTPTERM", "OK", 1000);

  esp_ota_handle_t h = 0;
  esp_err_t err = esp_ota_begin(part, total, &h);
  if (err != ESP_OK) {
    TEST_INFO(NAME, "esp_ota_begin: %s", esp_err_to_name(err));
    return false;
  }

  // --- Phase 1: ranged-window download loop ---
  const int kWindow = cfg::kOtaSimWindowSize;  // bytes per ranged GET (<< modem cache cap)
  uint8_t buf[256];
  int offset = 0, lastPct = -1;
  bool ok = true;
  uint32_t t0 = (uint32_t)xTaskGetTickCount();

  while (offset < total) {
    int win = total - offset;
    if (win > kWindow) win = kWindow;
    int end = offset + win - 1;

    // Fresh HTTP session per window: the modem accumulates each window's response
    // buffer across HTTPACTIONs within one HTTPINIT session, and after ~10 windows
    // (~320 KB) further AT commands return ERROR — so re-INIT to release it.
    simSendAt("AT+HTTPTERM", "OK", 1000);
    if (!simSendAt("AT+HTTPINIT", "OK", 2000)) { TEST_INFO(NAME, "HTTPINIT fail @%d", offset); ok = false; break; }
    std::snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
    if (!simSendAt(cmd, "OK", 2000)) { TEST_INFO(NAME, "set URL fail @%d", offset); ok = false; break; }

    // Per-window Range header.
    std::snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"USERDATA\",\"Range: bytes=%d-%d\"", offset, end);
    if (!simSendAt(cmd, "OK", 2000)) { TEST_INFO(NAME, "set Range fail @%d", offset); ok = false; break; }

    UartDrv::flushSim();
    TEST_INFO(NAME, "AT> AT+HTTPACTION=0 [%d-%d]", offset, end);
    UartDrv::writeLineSim("AT+HTTPACTION=0");
    std::string aline;
    if (!simWaitToken("+HTTPACTION:", 30000, &aline)) { TEST_INFO(NAME, "no +HTTPACTION @%d", offset); ok = false; break; }

    int m = 0, s = 0, len = 0;
    const char* q = std::strstr(aline.c_str(), "+HTTPACTION:");
    if (q) std::sscanf(q, "+HTTPACTION: %d,%d,%d", &m, &s, &len);
    if (s == 200) {  // modem/server ignored Range → would resend the whole file
      TEST_INFO(NAME, "Range NOT honored @%d (got 200, expected 206) — USERDATA unsupported", offset);
      ok = false; break;
    }
    if (s != 206 || len <= 0) { TEST_INFO(NAME, "bad window status=%d len=%d @%d", s, len, offset); ok = false; break; }

    // Read this window's `len` bytes from ONE AT+HTTPREAD=0,<len>. The modem then
    // streams the body as a run of "+HTTPREAD: <n>" sub-blocks (this firmware uses
    // 1024 B); consume them all from that single command. Do NOT issue another
    // HTTPREAD before the stream ends — a second HTTPREAD mid-stream returns ERROR.
    UartDrv::flushSim();
    std::snprintf(cmd, sizeof(cmd), "AT+HTTPREAD=0,%d", len);
    TEST_INFO(NAME, "AT> %s", cmd);
    if (!UartDrv::writeLineSim(cmd)) { ok = false; break; }

    int wgot = 0;
    while (wgot < len) {
      int blk = 0;
      std::string detail;
      int hr = readHttpReadLen(blk, cfg::kOtaSimReadTimeoutMs, detail);
      if (hr < 0)  { TEST_INFO(NAME, "HTTPREAD hdr fail(%d) @%d+%d: '%s'", hr, offset, wgot, detail.c_str()); ok = false; break; }
      if (blk <= 0) { TEST_INFO(NAME, "HTTPREAD early end @%d+%d (blk=%d)", offset, wgot, blk); ok = false; break; }

      int remaining = blk;
      uint32_t rd = (uint32_t)xTaskGetTickCount();
      while (remaining > 0) {
        int chunk = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int n = UartDrv::readSim(buf, chunk, 1000);
        if (n <= 0) {
          if (pdTICKS_TO_MS(xTaskGetTickCount() - rd) > cfg::kOtaSimReadTimeoutMs) break;
          continue;
        }
        if (esp_ota_write(h, buf, n) != ESP_OK) { ok = false; break; }
        remaining -= n;
        rd = (uint32_t)xTaskGetTickCount();
      }
      if (!ok || remaining != 0) { TEST_INFO(NAME, "read/write fail @%d+%d (rem=%d)", offset, wgot, remaining); ok = false; break; }
      wgot += blk;
    }
    if (!ok) break;

    // Consume the trailing "+HTTPREAD: 0" / "OK" that closes the window stream.
    drainToOk(1000);
    offset += wgot;

    int pct = (int)((int64_t)offset * 100 / total);
    if (pct >= lastPct + 5) {
      lastPct = pct;
      uint32_t el = pdTICKS_TO_MS(xTaskGetTickCount() - t0);
      unsigned bps = el ? (unsigned)((int64_t)offset * 1000 / el) : 0;
      TEST_INFO(NAME, "downloaded %d%% (%d/%d) %us %uB/s", pct, offset, total, (unsigned)(el / 1000), bps);
    }
  }

  uint32_t elapsed = pdTICKS_TO_MS(xTaskGetTickCount() - t0);
  simSendAt("AT+HTTPTERM", "OK", 1000);

  if (!ok || offset != total) {
    esp_ota_abort(h);
    TEST_INFO(NAME, "download incomplete %d/%d (%ums)", offset, total, (unsigned)elapsed);
    return false;
  }

  // esp_ota_end validates the written image (magic byte + SHA256). It does NOT
  // change the boot partition, so the running firmware stays intact.
  err = esp_ota_end(h);
  if (err != ESP_OK) {
    TEST_INFO(NAME, "esp_ota_end (verify) FAIL: %s", esp_err_to_name(err));
    return false;
  }

  TEST_INFO(NAME, "image verified OK (%d bytes, %ums) — boot NOT switched", total, (unsigned)elapsed);
  return true;
}

// ──────────────────────────────────────────────
// Entry points
// ──────────────────────────────────────────────
void test_ota() {
  TEST_START(NAME);
  TEST_INFO(NAME, "running fw: %s", cfg::kCurrentFwVersion);
  TEST_INFO(NAME, "version url: %s", cfg::kFirmwareVersionUrl);

  if (!simConnect()) {
    TEST_FAIL(NAME, "SIM connect failed");
    simDisconnect();
    return;
  }

  std::string body, remote;
  bool got = simHttpGet(cfg::kFirmwareVersionUrl, body);
  simDisconnect();

  if (!got) { TEST_FAIL(NAME, "version GET failed"); return; }
  TEST_INFO(NAME, "version body: '%s'", body.c_str());

  if (!parseFwVersion(body, remote)) { TEST_FAIL(NAME, "parse fw_version failed"); return; }

  TEST_INFO(NAME, "remote=%s  current=%s", remote.c_str(), cfg::kCurrentFwVersion);
  if (remote == cfg::kCurrentFwVersion) {
    TEST_INFO(NAME, "=> UP TO DATE");
  } else {
    TEST_INFO(NAME, "=> UPDATE AVAILABLE (run 'test otadl' to download+verify)");
  }
  TEST_PASS(NAME);
}

void test_ota_download() {
  TEST_START(NAME);
  TEST_INFO(NAME, "bin url: %s", cfg::kFirmwareBinUrl);
  TEST_INFO(NAME, "NOTE: downloads to inactive partition, verifies, NO reboot");

  if (!simConnect()) {
    TEST_FAIL(NAME, "SIM connect failed");
    simDisconnect();
    return;
  }

  bool ok = simHttpDownloadVerify(cfg::kFirmwareBinUrl);
  simDisconnect();

  if (ok) TEST_PASS(NAME);
  else TEST_FAIL(NAME, "download/verify failed");
}
