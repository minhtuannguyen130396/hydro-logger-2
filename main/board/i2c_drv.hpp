#pragma once
#include <cstdint>

class I2cDrv {
public:
  static bool init();
  static bool writeReg(uint8_t addr, uint8_t reg, const uint8_t* data, uint8_t len);
  static bool readReg(uint8_t addr, uint8_t reg, uint8_t* data, uint8_t len);
};
