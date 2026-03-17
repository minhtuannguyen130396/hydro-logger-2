#pragma once
#include "common/singleton.hpp"
#include "services/connectivity/comm_module.hpp"

class Sim4GModule : public ICommModule, public Singleton<Sim4GModule> {
  friend class Singleton<Sim4GModule>;
public:
  bool powerOn(LogBuffer& log) override;
  bool checkInternet(uint32_t timeoutMs, LogBuffer& log) override;
  bool sendPayload(const std::string& url, const std::string& json, LogBuffer& log) override;
  void powerOff(LogBuffer& log) override;
  CommType type() const override { return CommType::Sim4G; }

private:
  Sim4GModule() = default;
  bool sendAtOk(const char* cmd, uint32_t timeoutMs, LogBuffer& log);
  bool sendAtExpect(const char* cmd, const char* expect, uint32_t timeoutMs, LogBuffer& log);
  bool waitForToken(const char* expect, uint32_t timeoutMs, LogBuffer& log, std::string* matched = nullptr);
  bool configureApn(SimApnProfile profile, uint32_t timeoutMs, LogBuffer& log);
  const char* apnName(SimApnProfile profile) const;
  const char* apnString(SimApnProfile profile) const;
  SimApnProfile otherApn(SimApnProfile profile) const;
  bool ensureReadyForAt(LogBuffer& log);
  bool active_{false};
  SimApnProfile active_apn_{SimApnProfile::Viettel};
};
