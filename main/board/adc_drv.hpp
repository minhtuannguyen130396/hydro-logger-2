#pragma once
#include <cstdint>

class AdcDrv {
public:
  static bool init();
  static int readMilliVolts(); // simple placeholder
};
