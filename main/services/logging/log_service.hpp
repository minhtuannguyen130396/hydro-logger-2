#pragma once
#include "services/logging/log_buffer.hpp"

class LogService {
public:
  static LogBuffer createSessionLog();
};
