#include "services/net/server_api.hpp"
#include "services/net/http_client.hpp"
#include "common/config.hpp"

const char* ServerApi::measurementUrl() { return "http://example.com/api/measure"; }
const char* ServerApi::logUrl()         { return "http://donuoctrieuduong.xyz/hydro-logger-api/post_logger.php"; }
const char* ServerApi::sessionLogUrl()  { return "http://example.com/api/session-log"; }
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

const char* ServerApi::timeUrl() {
  return "http://donuoctrieuduong.xyz/dev_test/get_time.php";
}

bool ServerApi::fetchServerTime(std::string& out, LogBuffer& log) {
  log.appendf("[API] get server time\n");
  return HttpClient::getText(timeUrl(), out, 8000);
}

const char* ServerApi::waterLevelUrl() {
  return "http://donuoctrieuduong.xyz/dev_test/water_lever.php";
}

bool ServerApi::sendWaterLevel(const std::string& json, LogBuffer& log) {
  log.appendf("[API] sendWaterLevel\n");
  return HttpClient::postJson(waterLevelUrl(), json, 8000);
}
