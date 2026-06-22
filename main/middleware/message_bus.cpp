#include "middleware/message_bus.hpp"
#include "common/config.hpp"

bool MessageBus::init() {
  q_measure_ = xQueueCreate(cfg::kMeasureQueueLen, sizeof(MeasurementMsg));
  q_log_     = xQueueCreate(cfg::kLogQueueLen, sizeof(LogMsg));
  if (!q_measure_ || !q_log_) return false;

  const bool measure_ok = measure_mirror_.init() && measure_mirror_.restoreToRam(q_measure_);
  const bool log_ok = log_mirror_.init() && log_mirror_.restoreToRam(q_log_);
  return measure_ok && log_ok;
}

bool MessageBus::publishMeasurement(const MeasurementMsg& msg) {
  return measure_mirror_.enqueue(q_measure_, msg);
}

bool MessageBus::publishLog(const LogMsg& msg) {
  return log_mirror_.enqueue(q_log_, msg);
}

bool MessageBus::peekMeasurement(MeasurementMsg& out, uint32_t timeoutMs) {
  return measure_mirror_.peek(q_measure_, out, timeoutMs);
}

bool MessageBus::peekLog(LogMsg& out, uint32_t timeoutMs) {
  return log_mirror_.peek(q_log_, out, timeoutMs);
}

bool MessageBus::popMeasurement(MeasurementMsg& out, uint32_t timeoutMs) {
  return measure_mirror_.pop(q_measure_, out, timeoutMs);
}

bool MessageBus::popLog(LogMsg& out, uint32_t timeoutMs) {
  return log_mirror_.pop(q_log_, out, timeoutMs);
}

bool MessageBus::ackMeasurement() {
  return measure_mirror_.ack(q_measure_);
}

bool MessageBus::ackLog() {
  return log_mirror_.ack(q_log_);
}

size_t MessageBus::measureDepth() const { return q_measure_ ? uxQueueMessagesWaiting(q_measure_) : 0; }
size_t MessageBus::logDepth() const { return q_log_ ? uxQueueMessagesWaiting(q_log_) : 0; }
