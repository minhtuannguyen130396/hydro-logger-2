#pragma once
#include <string>
#include "services/logging/log_buffer.hpp"

class OtaService {
public:
  bool checkAndUpdate(const std::string& versionUrl, const std::string& binUrl, LogBuffer& log);
};
