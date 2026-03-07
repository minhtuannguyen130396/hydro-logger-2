#pragma once
#include "middleware/message_bus.hpp"
#include "app/app_state.hpp"

struct AppContext {
  MessageBus bus;
  AppState state;
};
