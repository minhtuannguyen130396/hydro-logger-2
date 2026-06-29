#pragma once
#include <string>
#include "services/logging/log_buffer.hpp"

class ICommModule;

class OtaService {
public:
  /// Full OTA flow: fetch version → compare → download+flash → set boot partition.
  /// The version fetch and binary download are routed through `active`: a SIM
  /// (4G) module uses its AT-command HTTP stack, anything else uses the ESP-IDF
  /// HTTP/HTTPS stack over a TCP/IP netif (Wi-Fi).
  /// Returns true if an update was performed successfully (caller should restart).
  /// Returns false if no update is needed or all attempts failed.
  bool checkAndUpdate(ICommModule* active, const std::string& versionUrl,
                      const std::string& binUrl, LogBuffer& log);

  /// Lightweight version check: fetch remote version JSON and compare.
  /// Returns true if a newer version is available on the server.
  bool checkVersionAvailable(ICommModule* active, const std::string& versionUrl, LogBuffer& log);

  bool fetchAndParseVersion(ICommModule* active, const std::string& versionUrl,
                            std::string& remoteVersion, LogBuffer& log);
  bool needsUpdate(const std::string& remoteVersion);

private:
  bool performOta(ICommModule* active, const std::string& binUrl, LogBuffer& log);
};
