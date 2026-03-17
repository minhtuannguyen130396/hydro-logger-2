#pragma once
#include <cstdint>
#include <string>

class UartDrv {
public:
  static bool initSimUart();
  static bool initSensorUart();
  static int writeSim(const uint8_t* data, int len);
  static int writeSensor(const uint8_t* data, int len);
  static int readSim(uint8_t* out, int maxLen, uint32_t timeoutMs);
  static int readSensor(uint8_t* out, int maxLen, uint32_t timeoutMs);
  static void flushSim();
  static void flushSensor();
  static bool writeLineSim(const char* line);
  static std::string readLineSim(uint32_t timeoutMs);
};
