#pragma once
#include <string>
#include <cstdint>

class HttpClient {
public:
  // Simple POST JSON. Returns true on HTTP 2xx.
  static bool postJson(const std::string& url, const std::string& json, uint32_t timeoutMs);
  // Simple GET into string (best effort).
  static bool getText(const std::string& url, std::string& out, uint32_t timeoutMs);
};
