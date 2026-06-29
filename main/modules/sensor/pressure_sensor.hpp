#pragma once
#include "common/singleton.hpp"
#include "modules/sensor/sensor.hpp"

// Analog pressure sensor read from L_SIGNAL (ADC1_CH7 / GPIO35).
//
// The measured value is reported through the ISensor distance interface so it
// flows unchanged into the existing measurement pipeline and the water_lever
// API (water_lever_0..2). The reported value is the averaged raw ADC count;
// converting it to a physical level/pressure can be done later via calibration.
class PressureSensor : public ISensor, public Singleton<PressureSensor> {
  friend class Singleton<PressureSensor>;
public:
  bool warmup(LogBuffer& log) override;
  bool readDistanceMm(int& outMm, LogBuffer& log) override;
  void finishMeasurement(LogBuffer& log) override;
  SensorType type() const override { return SensorType::Pressure; }

private:
  PressureSensor() = default;
};
