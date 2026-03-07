#pragma once
#include "services/connectivity/comm_module.hpp"
#include "middleware/message_bus.hpp"
#include "services/logging/log_buffer.hpp"

class ConnectivityManager {
public:
  bool warmup(LogBuffer& log);
  bool sendQueued(MessageBus& bus, uint32_t windowMs, LogBuffer& log);

  ICommModule* active() const { return active_; }

private:
  ICommModule* selectPreferred();
  ICommModule* other(ICommModule* m);

  bool tryModule(ICommModule* m, LogBuffer& log);

  ICommModule* active_{nullptr};
};
