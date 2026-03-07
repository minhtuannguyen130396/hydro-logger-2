#include "modules/sensor/sensor_manager.hpp"
#include "modules/sensor/laser_sensor.hpp"
#include "modules/sensor/ultrasonic_sensor.hpp"
#include "common/nvs_store.hpp"

ISensor* SensorManager::selectPreferred() {
  SensorType last = NvsStore::getLastWorkingSensor(SensorType::Laser);
  return (last == SensorType::Laser) ? (ISensor*)&LaserSensor::instance()
                                     : (ISensor*)&UltrasonicSensor::instance();
}

ISensor* SensorManager::other(ISensor* s) {
  if (!s) return (ISensor*)&LaserSensor::instance();
  return (s->type() == SensorType::Laser) ? (ISensor*)&UltrasonicSensor::instance()
                                         : (ISensor*)&LaserSensor::instance();
}

void SensorManager::updateLastWorking(ISensor* s) {
  if (!s) return;
  NvsStore::setLastWorkingSensor(s->type());
}

bool SensorManager::isValidDistance(int mm) const {
  return (mm > 0) && (mm <= cfg::kMaxDistanceMm);
}

bool SensorManager::ensureReady(ISensor*& outActive, LogBuffer& log) {
  ISensor* first = selectPreferred();
  ISensor* second = other(first);

  log.appendf("[SensorManager] preferred=%s\n", first->type()==SensorType::Laser ? "Laser":"Ultrasonic");

  if (first->warmup(log)) {
    outActive = first;
    log.appendf("[SensorManager] active=%s\n", outActive->type()==SensorType::Laser ? "Laser":"Ultrasonic");
    return true;
  }

  log.appendf("[SensorManager] preferred warmup FAIL -> switch\n");
  if (second->warmup(log)) {
    outActive = second;
    updateLastWorking(outActive);
    log.appendf("[SensorManager] active=%s (updated last)\n", outActive->type()==SensorType::Laser ? "Laser":"Ultrasonic");
    return true;
  }

  log.appendf("[SensorManager] both sensors FAIL\n");
  outActive = nullptr;
  return false;
}

bool SensorManager::read3(ISensor* active, int out[cfg::kDistanceSamples], LogBuffer& log) {
  if (!active) return false;

  for (int i = 0; i < cfg::kDistanceSamples; ++i) {
    int mm = 0;
    bool ok = false;
    for (int r = 0; r < cfg::kMaxRepeatRead; ++r) {
      if (!active->readDistanceMm(mm, log)) {
        log.appendf("[SensorManager] read fail (try %d)\n", r+1);
        continue;
      }
      if (!isValidDistance(mm)) {
        log.appendf("[SensorManager] invalid dist=%d (try %d)\n", mm, r+1);
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
