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
  testDelayMs(2000);

  // Build Modbus read request: addr=0x01, func=0x03, start=0x0002, count=0x0005
  uint8_t request[8];
  request[0] = 0x01; // node address
  request[1] = 0x03; // read holding registers
  request[2] = 0x00; request[3] = 0x02; // start address
  request[4] = 0x00; request[5] = 0x01; // register count
  uint16_t crc = modbusCrc16(request, 6);
  request[6] = (uint8_t)(crc & 0xFF);
  request[7] = (uint8_t)(crc >> 8);

  // Send request
  UartDrv::flushSensor();
  TEST_INFO(ULTRA_NAME, "Sending Modbus read request...");
  int written = UartDrv::writeSensor(request, sizeof(request));
  if (written != sizeof(request)) {
    TEST_FAIL(ULTRA_NAME, "UART write failed");
    IoController::instance().setUltrasonicPower(false);
    return;
  }

  // Read response (expect 15 bytes)
  testDelayMs(6000); // inter-frame delay
  uint8_t response[32]{};
  int rx = UartDrv::readSensor(response, sizeof(response), 6000);

  if (rx >= 15) {
    // Verify CRC
    uint16_t expCrc = modbusCrc16(response, rx - 2);
    uint16_t gotCrc = (uint16_t)response[rx - 2] | ((uint16_t)response[rx - 1] << 8);

    if (expCrc == gotCrc) {
      uint16_t dist = ((uint16_t)response[3] << 8) | response[4];
      uint16_t range = ((uint16_t)response[5] << 8) | response[6];
      int16_t offset = (int16_t)(((uint16_t)response[7] << 8) | response[8]);
      uint16_t baud = ((uint16_t)response[9] << 8) | response[10];
      uint16_t node = ((uint16_t)response[11] << 8) | response[12];

      TEST_INFO(ULTRA_NAME, "Distance: %u mm", (unsigned)dist);
      TEST_INFO(ULTRA_NAME, "Range:    %u mm", (unsigned)range);
      TEST_INFO(ULTRA_NAME, "Offset:   %d mm", (int)offset);
      TEST_INFO(ULTRA_NAME, "Baudrate: %u", (unsigned)baud);
      TEST_INFO(ULTRA_NAME, "Node:     %u", (unsigned)node);
      TEST_PASS(ULTRA_NAME);
    } else {
      TEST_INFO(ULTRA_NAME, "CRC mismatch: expected=0x%04X got=0x%04X", expCrc, gotCrc);
      TEST_FAIL(ULTRA_NAME, "CRC error");
    }
  }
  else {
    TEST_INFO(ULTRA_NAME, "Response too short: %d bytes (expected 15)", rx);
    if (rx > 0) {
      char hexBuf[96]{};
      int pos = 0;
      for (int i = 0; i < rx && pos < (int)sizeof(hexBuf) - 4; i++)
        pos += snprintf(hexBuf + pos, sizeof(hexBuf) - pos, "%02X ", response[i]);
      TEST_INFO(ULTRA_NAME, "RX data: %s", hexBuf);
    }
    TEST_FAIL(ULTRA_NAME, "No valid response - check sensor connection");
  }

  // Power off
  // TEST_INFO(ULTRA_NAME, "Power OFF");
  // IoController::instance().setUltrasonicPower(false);
}
