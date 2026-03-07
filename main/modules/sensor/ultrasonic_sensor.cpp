#include "modules/sensor/ultrasonic_sensor.hpp"
#include "modules/io/io_controller.hpp"
#include "common/config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

bool UltrasonicSensor::warmup(LogBuffer& log) {
  log.appendf("[Ultrasonic] power on\n");
  IoController::instance().setUltrasonicPower(true);
  vTaskDelay(pdMS_TO_TICKS(cfg::kSensorWarmupMs));

  // TODO: implement real handshake.
  log.appendf("[Ultrasonic] warmup OK\n");
  return true;
}

bool UltrasonicSensor::readDistanceMm(int& outMm, LogBuffer& log) {
  // TODO: implement real read.
  static int fake = 980;
  fake += 3;
  outMm = fake;
  log.appendf("[Ultrasonic] dist=%dmm\n", outMm);
  return true;
}
