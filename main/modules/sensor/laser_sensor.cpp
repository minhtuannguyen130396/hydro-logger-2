#include "modules/sensor/laser_sensor.hpp"

#include <cstdint>
#include <cstdio>

#include "board/pins.hpp"
#include "board/uart_drv.hpp"
#include "common/config.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modules/io/io_controller.hpp"

namespace {

static const char* TAG = "Laser";

constexpr uint8_t kFrameHeader = 0xAA;
constexpr uint32_t kLaserResponseTimeoutMs = 2000;
constexpr uint32_t kInterChunkTimeoutMs = 50;
constexpr uint32_t kLaserPowerEdgeDelayMs = 100;
constexpr uint32_t kLaserBootDelayMs = 3000;
constexpr int kMinDistanceFrameBytes = 10;
constexpr int kMaxResponseBytes = 30;

constexpr uint8_t kLaserOnCommand[] = {
    0xAA, 0x00, 0x01, 0xBE, 0x00, 0x01, 0x00, 0x01, 0xC1};
constexpr uint8_t kLaserGetDistanceCommand[] = {
    0xAA, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x21};

void logRawFrame(const char* label, const uint8_t* data, int len, LogBuffer& log) {
  if (len <= 0) {
    ESP_LOGW(TAG, "[Laser] %s: no data received (len=0)", label);
    log.appendf("[Laser] %s: no data received (len=0)\n", label);
    return;
  }
  char hexStr[kMaxResponseBytes * 3 + 1]{};
  int offset = 0;
  for (int i = 0; i < len && i < kMaxResponseBytes; ++i) {
    offset += std::snprintf(hexStr + offset, sizeof(hexStr) - offset, "%02X ", data[i]);
  }
  ESP_LOGI(TAG, "[Laser] %s raw rx (%d bytes): %s", label, len, hexStr);
  log.appendf("[Laser] %s raw rx len=%d: %s\n", label, len, hexStr);
}

bool ensureSensorUart(LogBuffer& log) {
  static bool sensor_uart_ready = false;
  if (sensor_uart_ready) return true;

  sensor_uart_ready = UartDrv::initSensorUart();
  if (!sensor_uart_ready) {
    ESP_LOGE(TAG, "[Laser] sensor uart init FAIL");
    log.appendf("[Laser] sensor uart init FAIL\n");
    return false;
  }
  ESP_LOGI(TAG, "[Laser] sensor uart init OK baud=%d (TX=GPIO%d, RX=GPIO%d)",
           pins::UART_SENSOR_BAUD, pins::UART_SENSOR_TX, pins::UART_SENSOR_RX);
  log.appendf("[Laser] sensor uart init OK baud=%d (TX=GPIO%d, RX=GPIO%d)\n",
              pins::UART_SENSOR_BAUD, pins::UART_SENSOR_TX, pins::UART_SENSOR_RX);
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
    ESP_LOGW(TAG, "[Laser] write FAIL len=%d", len);
    log.appendf("[Laser] write FAIL len=%d\n", len);
    return false;
  }

  uint8_t frame[kMaxResponseBytes]{};
  const int rx = readResponseFrame(frame, sizeof(frame), kLaserResponseTimeoutMs);
  logRawFrame("ack", frame, rx, log);
  if (rx <= 0) {
    ESP_LOGW(TAG, "[Laser] ack timeout");
    log.appendf("[Laser] ack timeout\n");
    return false;
  }
  int hdr = -1;
  for (int i = 0; i < rx; ++i) {
    if (frame[i] == kFrameHeader) { hdr = i; break; }
  }
  if (hdr < 0) {
    ESP_LOGW(TAG, "[Laser] ack invalid header, rx=%d", rx);
    log.appendf("[Laser] ack invalid header, rx=%d\n", rx);
    return false;
  }

  ESP_LOGI(TAG, "[Laser] ack OK len=%d hdr=%d", rx, hdr);
  log.appendf("[Laser] ack OK len=%d hdr=%d\n", rx, hdr);
  return true;
}

bool sendDistanceCommand(int& outMm, LogBuffer& log) {
  UartDrv::flushSensor();
  if (UartDrv::writeSensor(kLaserGetDistanceCommand, sizeof(kLaserGetDistanceCommand)) !=
      sizeof(kLaserGetDistanceCommand)) {
    ESP_LOGW(TAG, "[Laser] write distance cmd FAIL");
    log.appendf("[Laser] write distance cmd FAIL\n");
    return false;
  }

  uint8_t frame[kMaxResponseBytes]{};
  const int rx = readResponseFrame(frame, sizeof(frame), kLaserResponseTimeoutMs);
  logRawFrame("dist", frame, rx, log);
  if (rx < kMinDistanceFrameBytes) {
    ESP_LOGW(TAG, "[Laser] distance frame too short len=%d", rx);
    log.appendf("[Laser] distance frame too short len=%d\n", rx);
    return false;
  }
  for (int i = 0; i <= rx - kMinDistanceFrameBytes; ++i) {
    if (frame[i] == kFrameHeader) {
      uint32_t distance = 0;
      for (int j = 6; j < 10; ++j) {
        distance = (distance << 8) | frame[i + j];
      }
      if (distance == 8448 && (i + 13) <= rx) {
        continue;
      }
      outMm = static_cast<int>(distance);
      ESP_LOGI(TAG, "[Laser] dist=%dmm frame_len=%d hdr=%d", outMm, rx, i);
      log.appendf("[Laser] dist=%dmm frame_len=%d hdr=%d\n", outMm, rx, i);
      return true;
    }
  }

  ESP_LOGW(TAG, "[Laser] distance invalid frame rx=%d", rx);
  log.appendf("[Laser] distance invalid frame rx=%d\n", rx);
  return false;
}

} // namespace

void LaserSensor::finishMeasurement(LogBuffer& log) {
  IoController::instance().setLaserPower(false);
  ESP_LOGI(TAG, "power OFF (idle HIGH)");
  log.appendf("[Laser] power OFF (idle HIGH)\n");
}

bool LaserSensor::warmup(LogBuffer& log) {
  // Laser power is active-low: drive line OFF (HIGH) then ON (LOW).
  ESP_LOGI(TAG, "power ON (seq HIGH->LOW), wait boot %lums", (unsigned long)kLaserBootDelayMs);
  log.appendf("[Laser] power ON (seq HIGH->LOW)\n");
  IoController::instance().setLaserPower(false);
  vTaskDelay(pdMS_TO_TICKS(kLaserPowerEdgeDelayMs));
  IoController::instance().setLaserPower(true);
  log.appendf("[Laser] wait boot %dms\n", (int)kLaserBootDelayMs);
  vTaskDelay(pdMS_TO_TICKS(kLaserBootDelayMs));

  if (!ensureSensorUart(log)) {
    finishMeasurement(log);
    return false;
  }

  for (int i = 0; i < cfg::kSensorHandshakeRetries; ++i) {
    ESP_LOGI(TAG, "handshake try %d", i + 1);
    log.appendf("[Laser] handshake try %d\n", i + 1);
    if (sendCommandAndWaitAck(kLaserOnCommand, sizeof(kLaserOnCommand), log)) {
      ESP_LOGI(TAG, "warmup OK");
      log.appendf("[Laser] warmup OK\n");
      return true;
    }
  }

  ESP_LOGW(TAG, "warmup FAIL");
  log.appendf("[Laser] warmup FAIL\n");
  finishMeasurement(log);
  return false;
}

bool LaserSensor::readDistanceMm(int& outMm, LogBuffer& log) {
  return sendDistanceCommand(outMm, log);
}

bool LaserSensor::turnOnByCommand(LogBuffer& log) {
  if (!ensureSensorUart(log)) {
    return false;
  }
  for (int i = 0; i < cfg::kSensorHandshakeRetries; ++i) {
    ESP_LOGI(TAG, "send ON command try %d", i + 1);
    log.appendf("[Laser] send ON command try %d\n", i + 1);
    if (sendCommandAndWaitAck(kLaserOnCommand, sizeof(kLaserOnCommand), log)) {
      ESP_LOGI(TAG, "laser ON OK");
      log.appendf("[Laser] laser ON OK\n");
      return true;
    }
  }
  ESP_LOGW(TAG, "laser ON FAIL");
  log.appendf("[Laser] laser ON FAIL\n");
  return false;
}
