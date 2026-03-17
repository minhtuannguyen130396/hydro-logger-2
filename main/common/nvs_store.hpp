#pragma once
#include <cstdint>

enum class CommType   : uint8_t { Sim4G=0, Dcom=1 };
enum class SimApnProfile : uint8_t { Viettel=0, Vinaphone=1 };

class NvsStore {
public:
  static bool init();

  static CommType getLastSuccessComm(CommType def = CommType::Sim4G);
  static void setLastSuccessComm(CommType t);

  static SimApnProfile getLastSimApn(SimApnProfile def = SimApnProfile::Viettel);
  static void setLastSimApn(SimApnProfile profile);
};
