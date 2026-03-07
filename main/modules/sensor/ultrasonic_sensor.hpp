#pragma once
#include "common/singleton.hpp"
#include "modules/sensor/sensor.hpp"

class UltrasonicSensor : public ISensor, public Singleton<UltrasonicSensor> {
  friend class Singleton<UltrasonicSensor>;
public:
  bool warmup(LogBuffer& log) override;
  bool readDistanceMm(int& outMm, LogBuffer& log) override;
  SensorType type() const override { return SensorType::Ultrasonic; }

private:
  UltrasonicSensor() = default;
};
