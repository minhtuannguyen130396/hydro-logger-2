#pragma once
#include <cstdint>

#include "services/logging/log_buffer.hpp"

enum class SensorType : uint8_t { Laser=0, Ultrasonic=1 };

class ISensor {
public:
  virtual ~ISensor() = default;
  virtual bool warmup(LogBuffer& log) = 0;
  virtual bool readDistanceMm(int& outMm, LogBuffer& log) = 0;
  virtual void finishMeasurement(LogBuffer& log) { (void)log; }
  virtual SensorType type() const = 0;
};
