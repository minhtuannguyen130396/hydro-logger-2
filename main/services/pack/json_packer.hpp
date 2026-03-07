#pragma once
#include <string>
#include "middleware/data_models.hpp"

class JsonPacker {
public:
  static std::string packMeasurement(const MeasurementMsg& m);
  static std::string packLog(const LogMsg& l);
};
