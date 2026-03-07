#pragma once
#include "common/singleton.hpp"
#include "services/connectivity/comm_module.hpp"

class Sim4GModule : public ICommModule, public Singleton<Sim4GModule> {
  friend class Singleton<Sim4GModule>;
public:
  bool powerOn(LogBuffer& log) override;
  bool checkInternet(uint32_t timeoutMs, LogBuffer& log) override;
  bool sendPayload(const std::string& json, LogBuffer& log) override;
  void powerOff(LogBuffer& log) override;
  CommType type() const override { return CommType::Sim4G; }

private:
  Sim4GModule() = default;
  bool sendAtOk(const char* cmd, uint32_t timeoutMs, LogBuffer& log);
};
