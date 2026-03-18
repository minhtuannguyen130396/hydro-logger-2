#pragma once
#include <string>
#include "middleware/data_models.hpp"
#include "services/logging/log_buffer.hpp"

class JsonPacker {
public:
  static std::string packMeasurement(const MeasurementMsg& m);
  static std::string packLog(const LogMsg& l);
  static std::string packSessionLog(const LogBuffer& log);
};
