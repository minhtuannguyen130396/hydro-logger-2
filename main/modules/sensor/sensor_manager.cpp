#include "modules/sensor/sensor_manager.hpp"

#include "board/pins.hpp"

#if PINS_UART1_DEVICE == PINS_UART1_DEVICE_LASER
#include "modules/sensor/laser_sensor.hpp"
#elif PINS_UART1_DEVICE == PINS_UART1_DEVICE_SUPERSONIC
#include "modules/sensor/ultrasonic_sensor.hpp"
#endif

namespace {

const char* sensorName(SensorType type) {
  return (type == SensorType::Laser) ? "Laser" : "Ultrasonic";
}

} // namespace

ISensor* SensorManager::selected() {
#if PINS_UART1_DEVICE == PINS_UART1_DEVICE_LASER
  return &LaserSensor::instance();
#elif PINS_UART1_DEVICE == PINS_UART1_DEVICE_SUPERSONIC
  return &UltrasonicSensor::instance();
#else
  return nullptr;
#endif
}

bool SensorManager::isValidDistance(int mm) const {
  return (mm > 0) && (mm <= cfg::kMaxDistanceMm);
}

bool SensorManager::ensureReady(ISensor*& outActive, LogBuffer& log) {
  ISensor* sensor = selected();
  if (!sensor) {
    log.appendf("[SensorManager] no sensor selected by build macro\n");
    outActive = nullptr;
    return false;
  }

  log.appendf("[SensorManager] selected=%s (build)\n", sensorName(sensor->type()));
  if (!sensor->warmup(log)) {
    log.appendf("[SensorManager] warmup FAIL\n");
    outActive = nullptr;
    return false;
  }

  outActive = sensor;
  log.appendf("[SensorManager] active=%s\n", sensorName(sensor->type()));
  return true;
}

bool SensorManager::read3(ISensor* active, int out[cfg::kDistanceSamples], LogBuffer& log) {
  if (!active) return false;

  for (int i = 0; i < cfg::kDistanceSamples; ++i) {
    int mm = 0;
    bool ok = false;
    for (int r = 0; r < cfg::kMaxRepeatRead; ++r) {
      if (!active->readDistanceMm(mm, log)) {
        log.appendf("[SensorManager] read fail (try %d)\n", r + 1);
        continue;
      }
      if (!isValidDistance(mm)) {
        log.appendf("[SensorManager] invalid dist=%d (try %d)\n", mm, r + 1);
        continue;
      }
      ok = true;
      break;
    }
    if (!ok) {
      out[i] = 0;
      return false;
    }
    out[i] = mm;
  }
  return true;
}
