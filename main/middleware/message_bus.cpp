#include "middleware/message_bus.hpp"
#include "common/config.hpp"

bool MessageBus::init() {
  q_measure_ = xQueueCreate(cfg::kMeasureQueueLen, sizeof(MeasurementMsg));
  q_log_     = xQueueCreate(cfg::kLogQueueLen, sizeof(LogMsg));
  return q_measure_ && q_log_;
}

bool MessageBus::publishMeasurement(const MeasurementMsg& msg) {
  return xQueueSend(q_measure_, &msg, 0) == pdTRUE;
}

bool MessageBus::publishLog(const LogMsg& msg) {
  return xQueueSend(q_log_, &msg, 0) == pdTRUE;
}

bool MessageBus::popMeasurement(MeasurementMsg& out, uint32_t timeoutMs) {
  return xQueueReceive(q_measure_, &out, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

bool MessageBus::popLog(LogMsg& out, uint32_t timeoutMs) {
  return xQueueReceive(q_log_, &out, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

size_t MessageBus::measureDepth() const { return q_measure_ ? uxQueueMessagesWaiting(q_measure_) : 0; }
size_t MessageBus::logDepth() const { return q_log_ ? uxQueueMessagesWaiting(q_log_) : 0; }
