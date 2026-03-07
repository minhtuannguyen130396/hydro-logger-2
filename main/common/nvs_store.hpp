#pragma once
#include <cstdint>

enum class SensorType : uint8_t { Laser=0, Ultrasonic=1 };
enum class CommType   : uint8_t { Sim4G=0, Dcom=1 };

class NvsStore {
public:
  static bool init();

  static SensorType getLastWorkingSensor(SensorType def = SensorType::Laser);
  static void setLastWorkingSensor(SensorType t);

  static CommType getLastSuccessComm(CommType def = CommType::Sim4G);
  static void setLastSuccessComm(CommType t);
};
