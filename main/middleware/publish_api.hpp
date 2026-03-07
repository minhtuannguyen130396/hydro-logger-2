#pragma once
#include "middleware/message_bus.hpp"

class PublishApi {
public:
  static void setBus(MessageBus* bus);

  static bool publishMeasurement(const MeasurementMsg& msg);
  static bool publishLog(const LogMsg& msg);

private:
  static MessageBus* s_bus;
};
