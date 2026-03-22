#include "services/pack/json_packer.hpp"
#include "services/logging/log_buffer.hpp"
#include "common/config.hpp"
#include "common/nvs_store.hpp"
#include <cstdio>

static void append_datetime(std::string& s, const DateTime& t) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                t.year, t.month, t.day, t.hour, t.minute, t.second);
  s += buf;
}

std::string JsonPacker::packMeasurement(const MeasurementMsg& m) {
  std::string s;
  s.reserve(256);
  s += "{";
  s += "\"device_id\":\""; s += m.meta.device_id; s += "\",";
  s += "\"fw\":\""; s += m.meta.fw_version; s += "\",";
  s += "\"vol_mv\":"; s += std::to_string(m.meta.voltage_mv); s += ",";
  s += "\"time\":\""; append_datetime(s, m.time); s += "\",";
  s += "\"valid\":"; s += (m.valid ? "true" : "false"); s += ",";
  s += "\"dist_mm\":[";
  for (int i = 0; i < cfg::kDistanceSamples; ++i) {
    s += std::to_string(m.dist_mm[i]);
    if (i + 1 < cfg::kDistanceSamples) s += ",";
  }
  s += "]";
  s += "}";
  return s;
}

std::string JsonPacker::packLog(const LogMsg& l) {
  std::string s;
  s.reserve(256 + l.len);
  s += "{";
  s += "\"device_id\":\""; s += l.meta.device_id; s += "\",";
  s += "\"fw\":\""; s += l.meta.fw_version; s += "\",";
  s += "\"vol_mv\":"; s += std::to_string(l.meta.voltage_mv); s += ",";
  s += "\"time\":\""; append_datetime(s, l.time); s += "\",";
  s += "\"log\":\"";

  // basic JSON escaping (minimal)
  for (uint16_t i = 0; i < l.len; ++i) {
    char c = l.text[i];
    if (c == '\\' || c == '"') { s += '\\'; s += c; }
    else if (c == '\n') s += "\\n";
    else if (c == '\r') s += "\\r";
    else s += c;
  }
  s += "\"";
  s += "}";
  return s;
}

std::string JsonPacker::packWaterLevel(const MeasurementMsg& m) {
  // Required JSON format:
  // { "water_lever_0":<int>, "water_lever_1":<int>, "water_lever_2":<int>,
  //   "date_time":"YYYY-MM-DD HH:MM:SS", "serial_number":"TD_MW_00012",
  //   "type":"water_lever", "vol":<int> }
  std::string s;
  s.reserve(256);
  s += "{";
  s += "\"water_lever_0\":"; s += std::to_string(m.dist_mm[0]); s += ",";
  s += "\"water_lever_1\":"; s += std::to_string(m.dist_mm[1]); s += ",";
  s += "\"water_lever_2\":"; s += std::to_string(m.dist_mm[2]); s += ",";

  // date_time: "YYYY-MM-DD HH:MM:SS" (space-separated, not ISO T)
  char dt[32];
  std::snprintf(dt, sizeof(dt), "%04d-%02d-%02d %02d:%02d:%02d",
                m.time.year, m.time.month, m.time.day,
                m.time.hour, m.time.minute, m.time.second);
  s += "\"date_time\":\""; s += dt; s += "\",";

  char serial[20];
  NvsStore::getDeviceSerial(serial, sizeof(serial));
  s += "\"serial_number\":\""; s += serial; s += "\",";
  s += "\"type\":\"water_lever\",";
  s += "\"vol\":"; s += std::to_string(m.meta.voltage_mv);
  s += "}";
  return s;
}

std::string JsonPacker::packSessionLog(const LogBuffer& log) {
  std::string s;
  s.reserve(256 + log.size());
  char sessSerial[20];
  NvsStore::getDeviceSerial(sessSerial, sizeof(sessSerial));
  s += "{\"device_id\":\"";
  s += sessSerial;
  s += "\",\"fw\":\"";
  s += cfg::kCurrentFwVersion;
  s += "\",\"log\":\"";

  const char* text = log.c_str();
  int len = log.size();
  for (int i = 0; i < len; ++i) {
    char c = text[i];
    if (c == '\\' || c == '"') { s += '\\'; s += c; }
    else if (c == '\n') s += "\\n";
    else if (c == '\r') s += "\\r";
    else s += c;
  }
  s += "\"}";
  return s;
}
