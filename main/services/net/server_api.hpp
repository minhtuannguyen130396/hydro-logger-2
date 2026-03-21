#pragma once
#include <string>
#include "services/logging/log_buffer.hpp"

class ServerApi {
public:
  // Configure your endpoints here.
  static const char* measurementUrl();
  static const char* logUrl();
  static const char* fwVersionUrl();
  static const char* fwBinUrl();

  static bool sendMeasurement(const std::string& json, LogBuffer& log);
  static bool sendLog(const std::string& json, LogBuffer& log);
  static bool sendSessionLog(const std::string& json, LogBuffer& log);
  static bool fetchFirmwareVersionJson(std::string& out, LogBuffer& log);
  static const char* sessionLogUrl();

  static const char* timeUrl();
  static bool fetchServerTime(std::string& out, LogBuffer& log);
};
