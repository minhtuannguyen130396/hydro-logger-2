#include "modules/sensor/laser_sensor.hpp"
#include "modules/io/io_controller.hpp"
#include "common/config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

bool LaserSensor::warmup(LogBuffer& log) {
  log.appendf("[Laser] power on\n");
  IoController::instance().setLaserPower(true);
  vTaskDelay(pdMS_TO_TICKS(cfg::kSensorWarmupMs));

  // TODO: implement real handshake (UART/I2C) with retries & timeout.
  // Stub behavior: always succeed.
  for (int i = 0; i < cfg::kSensorHandshakeRetries; ++i) {
    log.appendf("[Laser] handshake try %d\n", i+1);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  log.appendf("[Laser] warmup OK\n");
  return true;
}

bool LaserSensor::readDistanceMm(int& outMm, LogBuffer& log) {
  // TODO: implement real read.
  // Stub: return a pseudo value.
  static int fake = 1234;
  fake += 7;
  outMm = fake;
  log.appendf("[Laser] dist=%dmm\n", outMm);
  return true;
}
