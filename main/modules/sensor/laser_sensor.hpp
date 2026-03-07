#pragma once
#include "common/singleton.hpp"
#include "modules/sensor/sensor.hpp"

class LaserSensor : public ISensor, public Singleton<LaserSensor> {
  friend class Singleton<LaserSensor>;
public:
  bool warmup(LogBuffer& log) override;
  bool readDistanceMm(int& outMm, LogBuffer& log) override;
  SensorType type() const override { return SensorType::Laser; }

private:
  LaserSensor() = default;
};
