#pragma once
#include "freertos/queue.h"
#include "middleware/message_bus.hpp"
#include "app/app_state.hpp"
#include "app/failover_task.hpp"

struct AppContext {
  MessageBus bus;
  AppState state;
  QueueHandle_t failover_q{nullptr};
};
