#pragma once
#include "common/singleton.hpp"
#include "services/connectivity/comm_module.hpp"

class DcomModule : public ICommModule, public Singleton<DcomModule> {
  friend class Singleton<DcomModule>;
public:
  bool powerOn(LogBuffer& log) override;
  bool checkInternet(uint32_t timeoutMs, LogBuffer& log) override;
  bool sendPayload(const std::string& json, LogBuffer& log) override;
  void powerOff(LogBuffer& log) override;
  CommType type() const override { return CommType::Dcom; }

private:
  DcomModule() = default;
};
