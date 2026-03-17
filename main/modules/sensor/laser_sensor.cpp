#include "modules/sensor/laser_sensor.hpp"

#include <cstdint>

#include "board/pins.hpp"
#include "board/uart_drv.hpp"
#include "common/config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modules/io/io_controller.hpp"

namespace {

constexpr uint8_t kFrameHeader = 0xAA;
constexpr uint32_t kLaserResponseTimeoutMs = 1000;
constexpr uint32_t kInterChunkTimeoutMs = 50;
constexpr uint32_t kLaserPowerEdgeDelayMs = 100;
constexpr uint32_t kLaserBootDelayMs = 3000;
constexpr int kMinDistanceFrameBytes = 10;
constexpr int kMaxResponseBytes = 32;

constexpr uint8_t kLaserOnCommand[] = {
    0xAA, 0x00, 0x01, 0xBE, 0x00, 0x01, 0x00, 0x01, 0xC1};
constexpr uint8_t kLaserGetDistanceCommand[] = {
    0xAA, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x21};

bool ensureSensorUart(LogBuffer& log) {
  static bool sensor_uart_ready = false;
  if (sensor_uart_ready) return true;

  sensor_uart_ready = UartDrv::initSensorUart();
  if (!sensor_uart_ready) {
    log.appendf("[Laser] sensor uart init FAIL\n");
    return false;
  }
  log.appendf("[Laser] sensor uart init OK baud=%d\n", pins::UART_SENSOR_BAUD);
  return true;
}

int readResponseFrame(uint8_t* out, int maxLen, uint32_t timeoutMs) {
  int total = 0;
  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < timeoutMs && total < maxLen) {
    const int n = UartDrv::readSensor(out + total, maxLen - total, kInterChunkTimeoutMs);
    if (n > 0) {
      total += n;
      continue;
    }
    if (total > 0) break;
  }
  return total;
}

bool sendCommandAndWaitAck(const uint8_t* cmd, int len, LogBuffer& log) {
  UartDrv::flushSensor();
  if (UartDrv::writeSensor(cmd, len) != len) {
    log.appendf("[Laser] write FAIL len=%d\n", len);
    return false;
  }

  uint8_t frame[kMaxResponseBytes]{};
  const int rx = readResponseFrame(frame, sizeof(frame), kLaserResponseTimeoutMs);
  if (rx <= 0) {
    log.appendf("[Laser] ack timeout\n");
    return false;
  }
  if (frame[0] != kFrameHeader) {
    log.appendf("[Laser] ack invalid header=0x%02X\n", frame[0]);
    return false;
  }

  log.appendf("[Laser] ack OK len=%d\n", rx);
  return true;
}

bool sendDistanceCommand(int& outMm, LogBuffer& log) {
  UartDrv::flushSensor();
  if (UartDrv::writeSensor(kLaserGetDistanceCommand, sizeof(kLaserGetDistanceCommand)) !=
      sizeof(kLaserGetDistanceCommand)) {
    log.appendf("[Laser] write distance cmd FAIL\n");
    return false;
  }

  uint8_t frame[kMaxResponseBytes]{};
  const int rx = readResponseFrame(frame, sizeof(frame), kLaserResponseTimeoutMs);
  if (rx < kMinDistanceFrameBytes) {
    log.appendf("[Laser] distance frame too short len=%d\n", rx);
    return false;
  }
  if (frame[0] != kFrameHeader) {
    log.appendf("[Laser] distance invalid header=0x%02X\n", frame[0]);
    return false;
  }

  uint32_t distance = 0;
  for (int i = 6; i < 10; ++i) {
    distance = (distance << 8) | frame[i];
  }

  outMm = static_cast<int>(distance);
  log.appendf("[Laser] dist=%dmm frame_len=%d\n", outMm, rx);
  return true;
}

} // namespace

void LaserSensor::finishMeasurement(LogBuffer& log) {
  IoController::instance().setLaserPower(true);
  log.appendf("[Laser] power idle HIGH\n");
}

bool LaserSensor::warmup(LogBuffer& log) {
  // Laser power-on is edge-triggered by driving the line HIGH then LOW.
  log.appendf("[Laser] power seq HIGH->LOW\n");
  IoController::instance().setLaserPower(true);
  vTaskDelay(pdMS_TO_TICKS(kLaserPowerEdgeDelayMs));
  IoController::instance().setLaserPower(false);
  log.appendf("[Laser] wait boot %dms\n", (int)kLaserBootDelayMs);
  vTaskDelay(pdMS_TO_TICKS(kLaserBootDelayMs));

  if (!ensureSensorUart(log)) {
    finishMeasurement(log);
    return false;
  }

  for (int i = 0; i < cfg::kSensorHandshakeRetries; ++i) {
    log.appendf("[Laser] handshake try %d\n", i + 1);
    if (sendCommandAndWaitAck(kLaserOnCommand, sizeof(kLaserOnCommand), log)) {
      log.appendf("[Laser] warmup OK\n");
      return true;
    }
  }

  log.appendf("[Laser] warmup FAIL\n");
  finishMeasurement(log);
  return false;
}

bool LaserSensor::readDistanceMm(int& outMm, LogBuffer& log) {
  return sendDistanceCommand(outMm, log);
}
