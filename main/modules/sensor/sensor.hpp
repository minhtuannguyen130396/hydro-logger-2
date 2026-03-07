#pragma once
#include "common/nvs_store.hpp"
#include "services/logging/log_buffer.hpp"

class ISensor {
public:
  virtual ~ISensor() = default;
  virtual bool warmup(LogBuffer& log) = 0;
  virtual bool readDistanceMm(int& outMm, LogBuffer& log) = 0;
  virtual SensorType type() const = 0;
};
