#pragma once
#include <cstdint>

enum class Err : int32_t {
  Ok = 0,
  Timeout = -1,
  Io = -2,
  Invalid = -3,
  NoMem = -4,
  NotReady = -5,
  NotSupported = -6
};

inline bool ok(Err e) { return e == Err::Ok; }
