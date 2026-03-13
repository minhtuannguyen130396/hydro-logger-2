#include "modules/sensor/ultrasonic_sensor.hpp"
#include "modules/io/io_controller.hpp"
#include <array>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr uint8_t kUltrasonicNodeAddress = 0x01;
constexpr uint8_t kModbusReadHoldingRegisters = 0x03;
constexpr uint16_t kStartAddress = 0x0002;
constexpr uint16_t kRegisterCount = 0x0005;
constexpr uint32_t kWarmupDelayMs = 5000;
constexpr uint32_t kCharTimeoutMs = 6000;
constexpr uint32_t kInterFrameDelayMs = 6000;
constexpr size_t kRequestSize = 8;
constexpr size_t kResponseSize = 15;

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

void buildReadRequest(std::array<uint8_t, kRequestSize>& frame) {
  frame[0] = kUltrasonicNodeAddress;
  frame[1] = kModbusReadHoldingRegisters;
  frame[2] = static_cast<uint8_t>(kStartAddress >> 8);
  frame[3] = static_cast<uint8_t>(kStartAddress & 0xFF);
  frame[4] = static_cast<uint8_t>(kRegisterCount >> 8);
  frame[5] = static_cast<uint8_t>(kRegisterCount & 0xFF);
  const uint16_t crc = modbusCrc16(frame.data(), kRequestSize - 2);
  frame[6] = static_cast<uint8_t>(crc & 0xFF);
  frame[7] = static_cast<uint8_t>(crc >> 8);
}

bool setOnSensor() {
  // Reserved for future transport/power path setup.
  return true;
}

bool exchangeSensorFrame(const uint8_t* txData,
                         size_t txLen,
                         uint8_t* rxData,
                         size_t rxLen,
                         uint32_t interFrameDelayMs,
                         uint32_t charTimeoutMs) {
  (void)txData;
  (void)txLen;
  (void)rxData;
  (void)rxLen;
  (void)interFrameDelayMs;
  (void)charTimeoutMs;
  // Transport wiring will be implemented in the shared UART/RS485 layer later.
  return false;
}

bool parseReadResponse(const uint8_t* response,
                       size_t len,
                       uint16_t& distanceMm,
                       uint16_t& rangeMm,
                       int16_t& offsetMm,
                       uint16_t& baudrate,
                       uint16_t& nodeAddress,
                       LogBuffer& log) {
  if (len != kResponseSize) {
    log.appendf("[Ultrasonic] invalid resp len=%d\n", static_cast<int>(len));
    return false;
  }

  if (response[0] != kUltrasonicNodeAddress) {
    log.appendf("[Ultrasonic] unexpected node=%u\n", static_cast<unsigned>(response[0]));
    return false;
  }

  if (response[1] != kModbusReadHoldingRegisters) {
    log.appendf("[Ultrasonic] unexpected func=0x%02X\n", static_cast<unsigned>(response[1]));
    return false;
  }

  if (response[2] != 10) {
    log.appendf("[Ultrasonic] unexpected byteCount=%u\n", static_cast<unsigned>(response[2]));
    return false;
  }

  const uint16_t expectedCrc = modbusCrc16(response, len - 2);
  const uint16_t actualCrc =
      static_cast<uint16_t>(response[len - 2]) |
      (static_cast<uint16_t>(response[len - 1]) << 8);
  if (expectedCrc != actualCrc) {
    log.appendf("[Ultrasonic] crc mismatch exp=0x%04X got=0x%04X\n",
                static_cast<unsigned>(expectedCrc),
                static_cast<unsigned>(actualCrc));
    return false;
  }

  distanceMm = readU16Be(&response[3]);
  rangeMm = readU16Be(&response[5]);
  offsetMm = readI16Be(&response[7]);
  baudrate = readU16Be(&response[9]);
  nodeAddress = readU16Be(&response[11]);
  return true;
}

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

bool UltrasonicSensor::warmup(LogBuffer& log) {
  log.appendf("[Ultrasonic] power on\n");
  IoController::instance().setUltrasonicPower(true);
  if (!setOnSensor()) {
    log.appendf("[Ultrasonic] setOnSensor FAIL\n");
    return false;
  }

  vTaskDelay(pdMS_TO_TICKS(kWarmupDelayMs));

  uint16_t distanceMm = 0;
  uint16_t rangeMm = 0;
  int16_t offsetMm = 0;
  uint16_t baudrate = 0;
  uint16_t nodeAddress = 0;
  if (!readRegisters(distanceMm, rangeMm, offsetMm, baudrate, nodeAddress, log)) {
    return false;
  }

  log.appendf("[Ultrasonic] warmup OK dist=%umm range=%umm offset=%dmm baud=%u node=%u\n",
              static_cast<unsigned>(distanceMm),
              static_cast<unsigned>(rangeMm),
              static_cast<int>(offsetMm),
              static_cast<unsigned>(baudrate),
              static_cast<unsigned>(nodeAddress));
  return true;
}

bool UltrasonicSensor::readDistanceMm(int& outMm, LogBuffer& log) {
  uint16_t distanceMm = 0;
  uint16_t rangeMm = 0;
  int16_t offsetMm = 0;
  uint16_t baudrate = 0;
  uint16_t nodeAddress = 0;
  if (!readRegisters(distanceMm, rangeMm, offsetMm, baudrate, nodeAddress, log)) {
    return false;
  }

  outMm = static_cast<int>(distanceMm);
  log.appendf("[Ultrasonic] dist=%dmm\n", outMm);
  return true;
}
