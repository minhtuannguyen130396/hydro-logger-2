#pragma once
#include <cstdint>
#include <string>

class UartDrv {
public:
  static bool initSimUart();
  static int writeSim(const uint8_t* data, int len);
  static int readSim(uint8_t* out, int maxLen, uint32_t timeoutMs);
  static bool writeLineSim(const char* line);
  static std::string readLineSim(uint32_t timeoutMs);
};
