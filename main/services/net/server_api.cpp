#include "services/net/server_api.hpp"
#include "services/net/http_client.hpp"
#include "common/config.hpp"

const char* ServerApi::measurementUrl() { return "https://example.com/api/measure"; }
const char* ServerApi::logUrl()         { return "https://example.com/api/log"; }
const char* ServerApi::sessionLogUrl()  { return "https://example.com/api/session-log"; }
const char* ServerApi::fwVersionUrl()   { return cfg::kFirmwareVersionUrl; }
const char* ServerApi::fwBinUrl()       { return cfg::kFirmwareBinUrl; }

bool ServerApi::sendMeasurement(const std::string& json, LogBuffer& log) {
  log.appendf("[API] sendMeasurement\n");
  return HttpClient::postJson(measurementUrl(), json, 8000);
}

bool ServerApi::sendLog(const std::string& json, LogBuffer& log) {
  log.appendf("[API] sendLog\n");
  return HttpClient::postJson(logUrl(), json, 8000);
}

bool ServerApi::sendSessionLog(const std::string& json, LogBuffer& log) {
  log.appendf("[API] sendSessionLog\n");
  return HttpClient::postJson(sessionLogUrl(), json, 8000);
}

bool ServerApi::fetchFirmwareVersionJson(std::string& out, LogBuffer& log) {
  log.appendf("[API] get fw version\n");
  return HttpClient::getText(fwVersionUrl(), out, 8000);
}
