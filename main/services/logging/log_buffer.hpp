#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "common/config.hpp"

class LogBuffer {
public:
  LogBuffer();

  void clear();
  void appendf(const char* fmt, ...);
  const char* c_str() const { return buf_; }
  int size() const { return (int)len_; }
  int capacity() const { return cfg::kSessionLogSize; }

private:
  void vappendf(const char* fmt, va_list ap);

  char buf_[cfg::kSessionLogSize]{};
  uint16_t len_{0};
};
