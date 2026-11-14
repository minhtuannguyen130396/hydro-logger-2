#include "app/failover_task.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "app/app_context.hpp"
#include "middleware/publish_api.hpp"
#include "modules/rtc/rtc_pcf8563.hpp"
#include "board/adc_drv.hpp"
#include "services/connectivity/sim4g_module.hpp"
#include "services/connectivity/dcom_module.hpp"

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
  lm.meta.voltage_mv = AdcDrv::readMilliVolts();

  const char* failed_status = (req.from == CommType::Sim4G)
                                  ? Sim4GModule::instance().statusDetail()
                                  : DcomModule::instance().statusDetail();

  std::snprintf(lm.text, sizeof(lm.text),
                "[Failover] swap from %s to %s after %s. Cause: %s",
                comm_name(req.from),
                comm_name(req.to),
                reason_name(req.reason),
                failed_status);
  lm.len = static_cast<uint16_t>(std::min(std::strlen(lm.text), sizeof(lm.text) - 1));
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
