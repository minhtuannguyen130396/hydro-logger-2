#pragma once
#include <string>
#include "services/logging/log_buffer.hpp"

class OtaService {
public:
  /// Full OTA flow: fetch version → compare → download+flash → set boot partition.
  /// Returns true if update was performed successfully (caller should restart).
  /// Returns false if no update needed or all attempts failed.
  bool checkAndUpdate(const std::string& versionUrl, const std::string& binUrl, LogBuffer& log);

private:
  bool fetchAndParseVersion(const std::string& versionUrl, std::string& remoteVersion, LogBuffer& log);
  bool needsUpdate(const std::string& remoteVersion);
  bool performOta(const std::string& binUrl, LogBuffer& log);
};
