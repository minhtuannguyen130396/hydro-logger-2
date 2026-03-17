#pragma once
#include <string>
#include "common/nvs_store.hpp"
#include "services/logging/log_buffer.hpp"

class ICommModule {
public:
  virtual ~ICommModule() = default;
  virtual bool powerOn(LogBuffer& log) = 0;
  virtual bool checkInternet(uint32_t timeoutMs, LogBuffer& log) = 0;
  virtual bool sendPayload(const std::string& url, const std::string& json, LogBuffer& log) = 0;
  virtual void powerOff(LogBuffer& log) = 0;
  virtual CommType type() const = 0;
};
