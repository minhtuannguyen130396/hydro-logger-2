#include "app/failover_task.hpp"

#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "app/app_context.hpp"
#include "middleware/publish_api.hpp"
#include "modules/rtc/rtc_pcf8563.hpp"

static const char* TAG = "FailoverTask";

static const char* comm_name(CommType t) {
  return (t == CommType::Sim4G) ? "SIM4G" : "DCOM";
}

static const char* reason_name(FailoverReason reason) {
  switch (reason) {
    case FailoverReason::SyncWarmupFail: return "warmup failure";
    case FailoverReason::SyncSendFail:   return "sync send failure";
    default:                             return "unknown";
  }
}

static CommType other_comm(CommType t) {
  return (t == CommType::Sim4G) ? CommType::Dcom : CommType::Sim4G;
}

bool enqueue_failover_request(AppContext* ctx, CommType from, FailoverReason reason) {
  if (!ctx || !ctx->failover_q) return false;

  const FailoverRequest req{from, other_comm(from), reason};
  return xQueueOverwrite(ctx->failover_q, &req) == pdTRUE;
}

static void publish_swap_log(const FailoverRequest& req) {
  LogMsg lm{};
  RtcPcf8563::instance().getTime(lm.time);

  std::snprintf(lm.text, sizeof(lm.text),
                "[Failover] swap from %s to %s after %s",
                comm_name(req.from),
                comm_name(req.to),
                reason_name(req.reason));
  lm.len = static_cast<uint16_t>(std::strlen(lm.text));
  PublishApi::publishLog(lm);
}

extern "C" void failover_task_entry(void* arg) {
  auto* ctx = reinterpret_cast<AppContext*>(arg);
  if (!ctx || !ctx->failover_q) {
    vTaskDelete(nullptr);
    return;
  }

  while (true) {
    FailoverRequest req{};
    if (xQueueReceive(ctx->failover_q, &req, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    ESP_LOGI(TAG, "swap preference %s -> %s (%s)",
             comm_name(req.from),
             comm_name(req.to),
             reason_name(req.reason));
    NvsStore::setLastSuccessComm(req.to);
    publish_swap_log(req);
  }
}
