#pragma once
#include "freertos/queue.h"
#include "freertos/task.h"
#include "middleware/message_bus.hpp"
#include "app/app_state.hpp"
#include "app/failover_task.hpp"

struct AppContext {
  MessageBus bus;
  AppState state;
  QueueHandle_t failover_q{nullptr};

  // Task handles captured at creation so the orchestrator can notify them
  // directly (set by app_main).
  TaskHandle_t measure_h{nullptr};
  TaskHandle_t sync_h{nullptr};
};
