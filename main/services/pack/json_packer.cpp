#include "services/pack/json_packer.hpp"
#include "common/config.hpp"
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
