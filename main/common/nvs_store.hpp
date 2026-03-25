#pragma once
#include <cstdint>

enum class CommType   : uint8_t { Sim4G=0, Dcom=1 };
enum class SimApnProfile : uint8_t { Viettel=0, Vinaphone=1 };

class NvsStore {
public:
  static bool init();

  // Connectivity preferences
  static CommType getLastSuccessComm(CommType def = CommType::Sim4G);
  static void setLastSuccessComm(CommType t);

  static SimApnProfile getLastSimApn(SimApnProfile def = SimApnProfile::Vinaphone);
  static void setLastSimApn(SimApnProfile profile);

  // --- Device calibration / config (portal-configurable) ---

  // Device code: numeric part of serial (e.g. 100 -> "TD_MW_0100")
  static uint16_t getDeviceCode(uint16_t def = 100);
  static void setDeviceCode(uint16_t code);

  // Build serial string from stored code. Writes into `out`, max `maxLen` bytes.
  // Returns pointer to `out` for convenience.
  static const char* getDeviceSerial(char* out, int maxLen);

  // Water-level offset in mm.  final = measured - offset
  static int32_t getWaterLevelOffset(int32_t def = 0);
  static void setWaterLevelOffset(int32_t offset);

  // Voltage calibration factor K stored as K*1000 (fixed-point).
  // final_mV = measured_mV * K.  Default K=1.000 -> stored as 1000.
  static uint32_t getVoltageK(uint32_t def = 1000);
  static void setVoltageK(uint32_t k_x1000);

  // Batch save portal config (single NVS open/commit/close)
  struct PortalConfig {
    bool     save_code;
    uint16_t dev_code;
    bool     save_offset;
    int32_t  wl_offset;
    bool     save_k;
    uint32_t vol_k;
  };
  static bool savePortalConfig(const PortalConfig& cfg);
};
