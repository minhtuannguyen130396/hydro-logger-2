#include "middleware/publish_api.hpp"
MessageBus* PublishApi::s_bus = nullptr;

void PublishApi::setBus(MessageBus* bus) { s_bus = bus; }

bool PublishApi::publishMeasurement(const MeasurementMsg& msg) {
  return s_bus ? s_bus->publishMeasurement(msg) : false;
}

bool PublishApi::publishLog(const LogMsg& msg) {
  return s_bus ? s_bus->publishLog(msg) : false;
}
