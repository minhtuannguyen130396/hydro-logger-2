#pragma once
#include <cstdint>
#include <cstring>
#include "common/nvs_store.hpp"
#include "common/time_utils.hpp"
#include "common/config.hpp"

enum class MsgType : uint8_t { Measurement=0, Log=1 };

struct DeviceMeta {
  char device_id[32]{};
  char fw_version[16]{"0.1.0"};
  int voltage_mv{0};

  DeviceMeta() {
    NvsStore::getDeviceSerial(device_id, sizeof(device_id));
  }
};

struct MeasurementMsg {
  DateTime time{};
  int dist_mm[cfg::kDistanceSamples]{0,0,0};
  bool valid{false};
  DeviceMeta meta{};
};

struct LogMsg {
  DateTime time{};
  char text[cfg::kSessionLogSize]{};
  uint16_t len{0};
  DeviceMeta meta{};
};
