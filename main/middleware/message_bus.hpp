#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "middleware/data_models.hpp"
#include "middleware/persistent_queue_mirror.hpp"

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
  persistent_queue::PersistentQueueMirror<MeasurementMsg> measure_mirror_{
      "/storage/measure_queue.bin", cfg::kMeasureQueueLen};
  persistent_queue::PersistentQueueMirror<LogMsg> log_mirror_{
      "/storage/log_queue.bin", cfg::kLogQueueLen};
};
