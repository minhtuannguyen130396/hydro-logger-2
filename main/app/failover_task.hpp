#pragma once

#include "common/nvs_store.hpp"

struct AppContext;

enum class FailoverReason : uint8_t {
  SyncWarmupFail = 0,
  SyncSendFail = 1,
};

struct FailoverRequest {
  CommType from;
  CommType to;
  FailoverReason reason;
};

bool enqueue_failover_request(AppContext* ctx, CommType from, FailoverReason reason);

extern "C" void failover_task_entry(void* arg);
