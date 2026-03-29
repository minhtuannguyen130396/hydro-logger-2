#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "middleware/data_models.hpp"

class MessageBus {
public:
  bool init();

  bool publishMeasurement(const MeasurementMsg& msg);
  bool publishLog(const LogMsg& msg);

  bool peekMeasurement(MeasurementMsg& out, uint32_t timeoutMs);
  bool peekLog(LogMsg& out, uint32_t timeoutMs);
  bool popMeasurement(MeasurementMsg& out, uint32_t timeoutMs);
  bool popLog(LogMsg& out, uint32_t timeoutMs);
  bool ackMeasurement();
  bool ackLog();

  size_t measureDepth() const;
  size_t logDepth() const;

private:
  QueueHandle_t q_measure_{nullptr};
  QueueHandle_t q_log_{nullptr};
};
