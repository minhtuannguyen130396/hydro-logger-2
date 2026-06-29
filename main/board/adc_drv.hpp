#pragma once
#include <cstdint>

class AdcDrv {
public:
  static bool init();
  static int readMilliVolts();   // battery voltage (ESP_VOLTAGE_ADC_PIN), simple placeholder
  static int readLSignalRaw();   // averaged raw ADC count from L_SIGNAL (analog pressure)
};
