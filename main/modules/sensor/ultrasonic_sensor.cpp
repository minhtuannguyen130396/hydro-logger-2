#include "modules/sensor/ultrasonic_sensor.hpp"
#include "modules/io/io_controller.hpp"
#include "board/uart_drv.hpp"
#include "esp_log.h"
#include <array>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

static const char* TAG = "Ultrasonic";

constexpr uint8_t  kUltrasonicNodeAddress       = 0x01;
constexpr uint8_t  kModbusReadHoldingRegisters   = 0x03;
constexpr uint16_t kStartAddress                 = 0x0001;   // register 0x0001 = distance
constexpr uint16_t kRegisterCount                = 0x0005;   // read 5 registers total
constexpr uint32_t kWarmupDelayMs                = 10000;
constexpr uint32_t kCharTimeoutMs                = 6000;
constexpr uint32_t kInterFrameDelayMs            = 6000;
constexpr size_t   kRequestSize                  = 8;        // addr(1)+func(1)+startHi(1)+startLo(1)+cntHi(1)+cntLo(1)+crc(2)
constexpr size_t   kResponseSize                 = 15;       // addr(1)+func(1)+byteCount(1)+data(10)+crc(2)

// ---------------------------------------------------------------------------
// Modbus CRC-16 (polynomial 0xA001 reflected)
// ---------------------------------------------------------------------------
uint16_t modbusCrc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      const bool lsb = (crc & 0x0001U) != 0U;
      crc >>= 1;
      if (lsb) crc ^= 0xA001U;
    }
  }
  return crc;
}

uint16_t readU16Be(const uint8_t* data) {
  return (static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]);
}

int16_t readI16Be(const uint8_t* data) {
  return static_cast<int16_t>(readU16Be(data));
}

// ---------------------------------------------------------------------------
// Build Modbus RTU read-holding-registers request frame (8 bytes)
// ---------------------------------------------------------------------------
void buildReadRequest(std::array<uint8_t, kRequestSize>& frame) {
  frame[0] = kUltrasonicNodeAddress;
  frame[1] = kModbusReadHoldingRegisters;
  frame[2] = static_cast<uint8_t>(kStartAddress >> 8);
  frame[3] = static_cast<uint8_t>(kStartAddress & 0xFF);
  frame[4] = static_cast<uint8_t>(kRegisterCount >> 8);
  frame[5] = static_cast<uint8_t>(kRegisterCount & 0xFF);
  const uint16_t crc = modbusCrc16(frame.data(), kRequestSize - 2);
  frame[6] = static_cast<uint8_t>(crc & 0xFF);   // CRC low first (Modbus convention)
  frame[7] = static_cast<uint8_t>(crc >> 8);
}

static bool s_sensor_uart_ready = false;

bool setOnSensor() {
  // Ensure sensor UART is initialised (once)
  if (!s_sensor_uart_ready) {
    s_sensor_uart_ready = UartDrv::initSensorUart();
    if (!s_sensor_uart_ready) {
      ESP_LOGE(TAG, "sensor UART init FAIL");
      return false;
    }
    ESP_LOGI(TAG, "sensor UART init OK");
  }
  return true;
}

// ---------------------------------------------------------------------------
// exchangeSensorFrame — real Modbus RTU exchange over UartDrv sensor UART
//
// Flow:
//   1) flush RX buffer (discard stale data)
//   2) write TX frame
//   3) wait inter-frame delay so slave has time to process
//   4) accumulate RX bytes with per-chunk timeout until we have rxLen or timeout
// ---------------------------------------------------------------------------
bool exchangeSensorFrame(const uint8_t* txData,
                         size_t txLen,
                         uint8_t* rxData,
                         size_t rxLen,
                         uint32_t interFrameDelayMs,
                         uint32_t charTimeoutMs) {
  // --- Log TX frame ---
  {
    char hex[kRequestSize * 3 + 1]{};
    for (size_t i = 0; i < txLen && i < kRequestSize; ++i) {
      std::snprintf(&hex[i * 3], 4, "%02X ", txData[i]);
    }
    ESP_LOGI(TAG, "[MODBUS] TX>> [%d bytes] %s", (int)txLen, hex);
  }

  // 1) Flush stale RX data
  UartDrv::flushSensor();

  // 2) Transmit request
  int written = UartDrv::writeSensor(txData, (int)txLen);
  if (written != (int)txLen) {
    ESP_LOGE(TAG, "[MODBUS] TX write FAIL: wrote %d/%d", written, (int)txLen);
    return false;
  }

  // 3) Inter-frame delay — give the slave time to process and start responding
  vTaskDelay(pdMS_TO_TICKS(interFrameDelayMs));

  // 4) Accumulate RX bytes. Use repeated reads with per-chunk timeout.
  //    Total budget = charTimeoutMs.
  size_t received = 0;
  uint32_t t0 = (uint32_t)xTaskGetTickCount();

  while (received < rxLen) {
    uint32_t elapsed = pdTICKS_TO_MS(xTaskGetTickCount() - t0);
    if (elapsed >= charTimeoutMs) break;

    uint32_t remaining = charTimeoutMs - elapsed;
    // Read in chunks — timeout per chunk is min(remaining, 500ms) for responsiveness
    uint32_t chunkTimeout = remaining < 500 ? remaining : 500;

    int n = UartDrv::readSensor(rxData + received,
                                (int)(rxLen - received),
                                chunkTimeout);
    if (n > 0) {
      received += (size_t)n;
    }
  }

  // --- Log RX frame ---
  {
    char hex[kResponseSize * 3 + 1]{};
    for (size_t i = 0; i < received && i < kResponseSize; ++i) {
      std::snprintf(&hex[i * 3], 4, "%02X ", rxData[i]);
    }
    ESP_LOGI(TAG, "[MODBUS] RX<< [%d/%d bytes] %s",
             (int)received, (int)rxLen, hex);
  }

  if (received != rxLen) {
    ESP_LOGW(TAG, "[MODBUS] RX incomplete: got %d, expected %d",
             (int)received, (int)rxLen);
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// Parse Modbus read-holding-registers response
//
// Register map (starting from address 0x0001, 5 registers):
//   reg[0] = 0x0001 → distance (mm)          — primary measurement
//   reg[1] = 0x0002 → range / max range (mm)  — debug/config
//   reg[2] = 0x0003 → offset (mm, signed)     — debug/config
//   reg[3] = 0x0004 → baudrate code           — debug/config
//   reg[4] = 0x0005 → node address            — debug/config
// ---------------------------------------------------------------------------
bool parseReadResponse(const uint8_t* response,
                       size_t len,
                       uint16_t& distanceMm,
                       uint16_t& rangeMm,
                       int16_t& offsetMm,
                       uint16_t& baudrate,
                       uint16_t& nodeAddress,
                       LogBuffer& log) {
  // Validate frame length
  if (len != kResponseSize) {
    ESP_LOGW(TAG, "[PARSE] invalid resp len=%d (expected %d)",
             (int)len, (int)kResponseSize);
    log.appendf("[Ultrasonic] invalid resp len=%d\n", (int)len);
    return false;
  }

  // Validate slave address
  if (response[0] != kUltrasonicNodeAddress) {
    ESP_LOGW(TAG, "[PARSE] unexpected slave=0x%02X (expected 0x%02X)",
             response[0], kUltrasonicNodeAddress);
    log.appendf("[Ultrasonic] unexpected node=%u\n", (unsigned)response[0]);
    return false;
  }

  // Check for Modbus exception response (function code has bit 7 set)
  if (response[1] & 0x80) {
    uint8_t excCode = (len > 2) ? response[2] : 0xFF;
    ESP_LOGE(TAG, "[PARSE] Modbus exception func=0x%02X code=0x%02X",
             response[1], excCode);
    log.appendf("[Ultrasonic] Modbus exception func=0x%02X code=0x%02X\n",
                response[1], excCode);
    return false;
  }

  // Validate function code
  if (response[1] != kModbusReadHoldingRegisters) {
    ESP_LOGW(TAG, "[PARSE] unexpected func=0x%02X (expected 0x%02X)",
             response[1], kModbusReadHoldingRegisters);
    log.appendf("[Ultrasonic] unexpected func=0x%02X\n", (unsigned)response[1]);
    return false;
  }

  // Validate byte count (5 registers × 2 bytes = 10)
  if (response[2] != 10) {
    ESP_LOGW(TAG, "[PARSE] unexpected byteCount=%u (expected 10)", (unsigned)response[2]);
    log.appendf("[Ultrasonic] unexpected byteCount=%u\n", (unsigned)response[2]);
    return false;
  }

  // Validate CRC
  const uint16_t expectedCrc = modbusCrc16(response, len - 2);
  const uint16_t actualCrc =
      static_cast<uint16_t>(response[len - 2]) |
      (static_cast<uint16_t>(response[len - 1]) << 8);
  if (expectedCrc != actualCrc) {
    ESP_LOGW(TAG, "[PARSE] CRC mismatch expected=0x%04X got=0x%04X",
             (unsigned)expectedCrc, (unsigned)actualCrc);
    log.appendf("[Ultrasonic] crc mismatch exp=0x%04X got=0x%04X\n",
                (unsigned)expectedCrc, (unsigned)actualCrc);
    return false;
  }

  // Extract all 5 registers (data starts at byte 3)
  // reg[0] @ byte 3-4  = distance (mm)
  // reg[1] @ byte 5-6  = range (mm)
  // reg[2] @ byte 7-8  = offset (mm, signed)
  // reg[3] @ byte 9-10 = baudrate code
  // reg[4] @ byte 11-12 = node address
  distanceMm  = readU16Be(&response[3]);
  rangeMm     = readU16Be(&response[5]);
  offsetMm    = readI16Be(&response[7]);
  baudrate    = readU16Be(&response[9]);
  nodeAddress = readU16Be(&response[11]);
  distanceMm = rangeMm - distanceMm;
  ESP_LOGI(TAG, "[PARSE] OK reg[0x0001]=dist:%u  reg[0x0002]=range:%u  "
           "reg[0x0003]=offset:%d  reg[0x0004]=baud:%u  reg[0x0005]=node:%u",
           (unsigned)distanceMm, (unsigned)rangeMm,
           (int)offsetMm, (unsigned)baudrate, (unsigned)nodeAddress);

  return true;
}

// ---------------------------------------------------------------------------
// High-level: build request → exchange → parse
// ---------------------------------------------------------------------------
bool readRegisters(uint16_t& distanceMm,
                   uint16_t& rangeMm,
                   int16_t& offsetMm,
                   uint16_t& baudrate,
                   uint16_t& nodeAddress,
                   LogBuffer& log) {
  std::array<uint8_t, kRequestSize> request{};
  std::array<uint8_t, kResponseSize> response{};
  buildReadRequest(request);

  if (!exchangeSensorFrame(request.data(),
                           request.size(),
                           response.data(),
                           response.size(),
                           kInterFrameDelayMs,
                           kCharTimeoutMs)) {
    log.appendf("[Ultrasonic] transport read FAIL\n");
    return false;
  }

  return parseReadResponse(response.data(),
                           response.size(),
                           distanceMm,
                           rangeMm,
                           offsetMm,
                           baudrate,
                           nodeAddress,
                           log);
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================

bool UltrasonicSensor::warmup(LogBuffer& log) {
  ESP_LOGI(TAG, "power ON, warmup %lu ms", (unsigned long)kWarmupDelayMs);
  log.appendf("[Ultrasonic] power on\n");
  IoController::instance().setUltrasonicPower(true);
  if (!setOnSensor()) {
    log.appendf("[Ultrasonic] setOnSensor FAIL\n");
    return false;
  }

  vTaskDelay(pdMS_TO_TICKS(kWarmupDelayMs));

  uint16_t distanceMm = 0;
  uint16_t rangeMm = 0;
  int16_t  offsetMm = 0;
  uint16_t baudrate = 0;
  uint16_t nodeAddress = 0;
  if (!readRegisters(distanceMm, rangeMm, offsetMm, baudrate, nodeAddress, log)) {
    ESP_LOGW(TAG, "warmup register read FAIL");
    return false;
  }

  ESP_LOGI(TAG, "warmup OK dist=%umm range=%umm offset=%dmm baud=%u node=%u",
           (unsigned)distanceMm, (unsigned)rangeMm,
           (int)offsetMm, (unsigned)baudrate, (unsigned)nodeAddress);
  log.appendf("[Ultrasonic] warmup OK dist=%umm range=%umm offset=%dmm baud=%u node=%u\n",
              (unsigned)distanceMm,
              (unsigned)rangeMm,
              (int)offsetMm,
              (unsigned)baudrate,
              (unsigned)nodeAddress);
  return true;
}

bool UltrasonicSensor::readDistanceMm(int& outMm, LogBuffer& log) {
  uint16_t distanceMm = 0;
  uint16_t rangeMm = 0;
  int16_t  offsetMm = 0;
  uint16_t baudrate = 0;
  uint16_t nodeAddress = 0;
  if (!readRegisters(distanceMm, rangeMm, offsetMm, baudrate, nodeAddress, log)) {
    ESP_LOGW(TAG, "readDistanceMm FAIL");
    return false;
  }

  outMm = static_cast<int>(distanceMm);
  ESP_LOGI(TAG, "read dist=%dmm (range=%u offset=%d baud=%u node=%u)",
           outMm, (unsigned)rangeMm, (int)offsetMm,
           (unsigned)baudrate, (unsigned)nodeAddress);
  log.appendf("[Ultrasonic] dist=%dmm\n", outMm);
  return true;
}
