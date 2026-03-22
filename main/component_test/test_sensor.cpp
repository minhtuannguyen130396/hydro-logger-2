#include "test_common.hpp"
#include "board/uart_drv.hpp"
#include "board/pins.hpp"
#include "modules/io/io_controller.hpp"

// ──────────────────────────────────────────────
// Laser Sensor Test
// ──────────────────────────────────────────────
static const char* LASER_NAME = "LASER";

static const uint8_t kLaserOnCmd[] = {
    0xAA, 0x00, 0x01, 0xBE, 0x00, 0x01, 0x00, 0x01, 0xC1};
static const uint8_t kLaserDistCmd[] = {
    0xAA, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x21};

static int readLaserFrame(uint8_t* out, int maxLen, uint32_t timeoutMs) {
  int total = 0;
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs && total < maxLen) {
    int n = UartDrv::readSensor(out + total, maxLen - total, 50);
    if (n > 0) {
      total += n;
      continue;
    }
    if (total > 0) break;
  }
  return total;
}

void test_laser() {
  TEST_START(LASER_NAME);

  // Init UART1 at laser baud
  TEST_INFO(LASER_NAME, "Init UART1 baud=%d", pins::UART_SENSOR_LASER_BAUD);
  // Need to init with laser baud specifically
  bool uart_ok = UartDrv::initSensorUart();
  if (!uart_ok) {
    TEST_FAIL(LASER_NAME, "UART1 init failed");
    return;
  }

  // Power on (edge trigger)
  TEST_INFO(LASER_NAME, "Power ON (HIGH -> LOW edge)");
  IoController::instance().setLaserPower(true);
  testDelayMs(100);
  IoController::instance().setLaserPower(false);
  TEST_INFO(LASER_NAME, "Wait boot 3000ms...");
  testDelayMs(3000);

  // Handshake
  UartDrv::flushSensor();
  TEST_INFO(LASER_NAME, "Sending ON command...");
  int written = UartDrv::writeSensor(kLaserOnCmd, sizeof(kLaserOnCmd));
  if (written != sizeof(kLaserOnCmd)) {
    TEST_FAIL(LASER_NAME, "UART write failed");
    IoController::instance().setLaserPower(true);
    return;
  }

  uint8_t frame[32]{};
  int rx = readLaserFrame(frame, sizeof(frame), 1000);
  if (rx <= 0 || frame[0] != 0xAA) {
    TEST_INFO(LASER_NAME, "No ACK received (rx=%d)", rx);
    TEST_FAIL(LASER_NAME, "Handshake failed - check laser connection");
    IoController::instance().setLaserPower(true);
    return;
  }
  TEST_INFO(LASER_NAME, "Handshake OK (ack len=%d)", rx);

  // Read distance 3 times
  for (int i = 0; i < 3; i++) {
    testDelayMs(500);
    UartDrv::flushSensor();
    UartDrv::writeSensor(kLaserDistCmd, sizeof(kLaserDistCmd));

    rx = readLaserFrame(frame, sizeof(frame), 1000);
    if (rx >= 10 && frame[0] == 0xAA) {
      uint32_t dist = 0;
      for (int j = 6; j < 10; j++) {
        dist = (dist << 8) | frame[j];
      }
      TEST_INFO(LASER_NAME, "Reading %d: distance = %lu mm", i + 1, (unsigned long)dist);
    } else {
      TEST_INFO(LASER_NAME, "Reading %d: FAIL (rx=%d)", i + 1, rx);
    }
  }

  // Power off
  TEST_INFO(LASER_NAME, "Power OFF");
  IoController::instance().setLaserPower(true); // idle HIGH
  TEST_PASS(LASER_NAME);
}

// ──────────────────────────────────────────────
// Ultrasonic Sensor Test
// ──────────────────────────────────────────────
static const char* ULTRA_NAME = "ULTRASONIC";

static uint16_t modbusCrc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      bool lsb = (crc & 0x0001U) != 0U;
      crc >>= 1;
      if (lsb) crc ^= 0xA001U;
    }
  }
  return crc;
}

void test_ultrasonic() {
  TEST_START(ULTRA_NAME);

  // Init UART1
  TEST_INFO(ULTRA_NAME, "Init UART1 baud=%d", pins::UART_SENSOR_SUPERSONIC_BAUD);
  bool uart_ok = UartDrv::initSensorUart();
  if (!uart_ok) {
    TEST_FAIL(ULTRA_NAME, "UART1 init failed");
    return;
  }

  // Power on
  TEST_INFO(ULTRA_NAME, "Power ON");
  IoController::instance().setUltrasonicPower(true);
  TEST_INFO(ULTRA_NAME, "Wait warmup 2000ms...");
 
  // Read registers one by one: 0x0002..0x0006
  struct RegInfo {
    uint16_t addr;
    const char* name;
    bool isSigned;
  };
  static const RegInfo regs[] = {
    {0x0001, "Current Level", false},
    {0x0002, "Measure Range", false},
    {0x0003, "Base Offset",   true },
    {0x0004, "Baudrate",      false},
    {0x0005, "Device ID",     false},
  };

  bool allOk = true;
  while(1) {
     testDelayMs(20000);
    // Build Modbus request: func=0x03, read 1 register
    uint8_t request[8];
    request[0] = 0x01;
    request[1] = 0x03;
    request[2] = (uint8_t)(regs[0].addr >> 8);
    request[3] = (uint8_t)(regs[0].addr & 0xFF);
    request[4] = 0x00;
    request[5] = 0x05;
    uint16_t crc = modbusCrc16(request, 6);
    request[6] = (uint8_t)(crc & 0xFF);
    request[7] = (uint8_t)(crc >> 8);

    // Print TX raw
    char txHex[32]{};
    for (int i = 0; i < 8; i++)
      snprintf(txHex + i * 3, sizeof(txHex) - i * 3, "%02X ", request[i]);
    TEST_INFO(ULTRA_NAME, "[%s] TX: %s", regs[0].name, txHex);

    // Send
    UartDrv::flushSensor();
    int written = UartDrv::writeSensor(request, sizeof(request));
    if (written != sizeof(request)) {
      TEST_INFO(ULTRA_NAME, "[%s] UART write failed", regs[0].name);
      allOk = false;
      continue;
    }

    // Read response (expect 7 bytes: addr + func + byteCount + 2 data + 2 CRC)
    testDelayMs(1000);
    uint8_t response[32]{};
    int rx = UartDrv::readSensor(response, sizeof(response), 3000);

    // Print RX raw
    char rxHex[96]{};
    int pos = 0;
    for (int i = 0; i < rx && pos < (int)sizeof(rxHex) - 4; i++)
      pos += snprintf(rxHex + pos, sizeof(rxHex) - pos, "%02X ", response[i]);
    TEST_INFO(ULTRA_NAME, "[%s] RX (%d bytes): %s", regs[0].name, rx, rxHex);

    if (rx < 15) {
      TEST_INFO(ULTRA_NAME, "Response too short: expected 15, got %d", rx);
      allOk = false;
      continue;
    }

    // Verify CRC
    uint16_t expCrc = modbusCrc16(response, rx - 2);
    uint16_t gotCrc = (uint16_t)response[rx - 2] | ((uint16_t)response[rx - 1] << 8);
    if (expCrc != gotCrc) {
      TEST_INFO(ULTRA_NAME, "CRC mismatch: expected=0x%04X got=0x%04X", expCrc, gotCrc);
      allOk = false;
      continue;
    }

    // Parse all 5 registers from response
    uint8_t byteCount = response[2];
    TEST_INFO(ULTRA_NAME, "Byte count: %d (expected %d)", byteCount, 5 * 2);

    for (int r = 0; r < 5; r++) {
      uint8_t hi = response[3 + r * 2];
      uint8_t lo = response[4 + r * 2];
      uint16_t raw = ((uint16_t)hi << 8) | lo;

      if (regs[r].isSigned) {
        TEST_INFO(ULTRA_NAME, "[%s] raw=[%02X %02X] = %d (0x%04X)",
                  regs[r].name, hi, lo, (int)(int16_t)raw, raw);
      } else {
        TEST_INFO(ULTRA_NAME, "[%s] raw=[%02X %02X] = %u (0x%04X)",
                  regs[r].name, hi, lo, (unsigned)raw, raw);
      }

      // Extra decode for Current Level: try swapped bytes and sum of bytes
      if (r == 0) {
        uint16_t swapped = ((uint16_t)lo << 8) | hi;
        uint16_t sum = (uint16_t)hi + (uint16_t)lo;
        TEST_INFO(ULTRA_NAME, "[%s] swapped=[%02X %02X] = %u (0x%04X)",
                  regs[r].name, lo, hi, (unsigned)swapped, swapped);
        TEST_INFO(ULTRA_NAME, "[%s] byte1+byte2 = %u + %u = %u",
                  regs[r].name, (unsigned)hi, (unsigned)lo, (unsigned)sum);
      }
    }

    testDelayMs(1000); // gap between requests
  }

  if (allOk) {
    TEST_PASS(ULTRA_NAME);
  } else {
    TEST_FAIL(ULTRA_NAME, "One or more register reads failed");
  }

  // Power off
  // TEST_INFO(ULTRA_NAME, "Power OFF");
  // IoController::instance().setUltrasonicPower(false);
}
