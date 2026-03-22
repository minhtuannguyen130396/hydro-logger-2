#pragma once
#include "common/singleton.hpp"
#include "services/connectivity/comm_module.hpp"
#include "freertos/FreeRTOS.h"

class Sim4GModule : public ICommModule, public Singleton<Sim4GModule> {
  friend class Singleton<Sim4GModule>;
public:
  bool powerOn(LogBuffer& log) override;
  bool checkInternet(uint32_t timeoutMs, LogBuffer& log) override;
  bool sendPayload(const std::string& url, const std::string& json, LogBuffer& log) override;
  bool httpGet(const std::string& url, std::string& response, LogBuffer& log);
  void powerOff(LogBuffer& log) override;
  CommType type() const override { return CommType::Sim4G; }

private:
  Sim4GModule() = default;

  // Low-level AT communication
  bool sendAtOk(const char* cmd, uint32_t timeoutMs, LogBuffer& log);
  bool sendAtExpect(const char* cmd, const char* expect, uint32_t timeoutMs,
                    LogBuffer& log, std::string* matched = nullptr);
  bool waitForToken(const char* expect, uint32_t timeoutMs, LogBuffer& log,
                    std::string* matched = nullptr);

  // Phase 1: Power & AT handshake
  void powerOnHardware(LogBuffer& log);
  bool waitForAtReady(uint32_t timeoutMs, LogBuffer& log);

  // Phase 2: LTE mode configuration
  bool configureLteMode(LogBuffer& log);

  // Phase 3: Network readiness (deadline-based, 40s total)
  bool checkSimReady(TickType_t deadline, LogBuffer& log);
  bool waitLteRegistration(TickType_t deadline, LogBuffer& log);
  bool tryApnConnect(SimApnProfile profile, TickType_t deadline, LogBuffer& log);
  bool configureApnPhase(SimApnProfile profile, TickType_t deadline, LogBuffer& log);
  bool attachAndActivateData(TickType_t deadline, LogBuffer& log);
  bool verifyDataReady(TickType_t deadline, LogBuffer& log);

  // HTTP session helpers
  void httpTermSafe(LogBuffer& log);
  bool ensurePdpActive(LogBuffer& log);
  bool httpInitAndSetBearer(const char* method, LogBuffer& log);

  // Carrier detection & APN helpers
  SimApnProfile detectCarrier(LogBuffer& log);
  const char* apnName(SimApnProfile profile) const;
  const char* apnString(SimApnProfile profile) const;
  SimApnProfile otherApn(SimApnProfile profile) const;

  bool active_{false};
  bool http_inited_{false};
  SimApnProfile active_apn_{SimApnProfile::Vinaphone};
};
