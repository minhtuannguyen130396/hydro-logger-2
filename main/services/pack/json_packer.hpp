#pragma once
#include <string>
#include "middleware/data_models.hpp"
#include "services/logging/log_buffer.hpp"

class JsonPacker {
public:
  static std::string packMeasurement(const MeasurementMsg& m);
  static std::string packLog(const LogMsg& l);
  static std::string packSessionLog(const LogBuffer& log);

  /// Pack measurement data into water_lever JSON format for the Trieu Duong API.
  /// Maps dist_mm[0..2] -> water_lever_0..2, meta.voltage_mv -> vol.
  static std::string packWaterLevel(const MeasurementMsg& m);
};
