#include "services/connectivity/connectivity_manager.hpp"
#include "services/connectivity/sim4g_module.hpp"
#include "services/connectivity/dcom_module.hpp"
#include "common/nvs_store.hpp"
#include "common/config.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

ICommModule* ConnectivityManager::selectPreferred() {
  CommType last = NvsStore::getLastSuccessComm(CommType::Sim4G);
  return (last == CommType::Sim4G) ? (ICommModule*)&Sim4GModule::instance()
                                  : (ICommModule*)&DcomModule::instance();
}

ICommModule* ConnectivityManager::other(ICommModule* m) {
  if (!m) return (ICommModule*)&Sim4GModule::instance();
  return (m->type() == CommType::Sim4G) ? (ICommModule*)&DcomModule::instance()
                                       : (ICommModule*)&Sim4GModule::instance();
}

bool ConnectivityManager::tryModule(ICommModule* m, LogBuffer& log) {
  if (!m) return false;
  if (!m->powerOn(log)) return false;
  if (!m->checkInternet(cfg::kConnCheckTimeoutMs, log)) {
    m->powerOff(log);
    return false;
  }
  return true;
}

bool ConnectivityManager::warmup(LogBuffer& log) {
  ICommModule* first = selectPreferred();
  ICommModule* second = other(first);

  log.appendf("[Conn] preferred=%s\n", first->type()==CommType::Sim4G ? "SIM4G":"DCOM");

  if (tryModule(first, log)) {
    active_ = first;
    NvsStore::setLastSuccessComm(active_->type());
    log.appendf("[Conn] active=%s (saved)\n", active_->type()==CommType::Sim4G ? "SIM4G":"DCOM");
    return true;
  }

  log.appendf("[Conn] preferred FAIL -> switch\n");
  if (tryModule(second, log)) {
    active_ = second;
    NvsStore::setLastSuccessComm(active_->type());
    log.appendf("[Conn] active=%s (saved)\n", active_->type()==CommType::Sim4G ? "SIM4G":"DCOM");
    return true;
  }

  log.appendf("[Conn] both modules FAIL\n");
  active_ = nullptr;
  return false;
}

bool ConnectivityManager::sendQueued(MessageBus& bus, uint32_t windowMs, LogBuffer& log) {
  if (!active_) return false;

  uint32_t start = (uint32_t)xTaskGetTickCount();
  while (pdTICKS_TO_MS(xTaskGetTickCount() - start) < windowMs) {
    MeasurementMsg mm{};
    LogMsg lm{};
    if (bus.popMeasurement(mm, cfg::kQueuePopTimeoutMs)) {
      // The caller typically packs JSON; here we just demonstrate sending.
      // Keep this function generic (send payload already packed).
      log.appendf("[Conn] got measurement msg (valid=%d)\n", (int)mm.valid);
      // no direct send here; leave to SyncTask for packing + ServerApi.
      return true; // SyncTask handles loops; this is placeholder
    }
    if (bus.popLog(lm, cfg::kQueuePopTimeoutMs)) {
      log.appendf("[Conn] got log msg len=%d\n", (int)lm.len);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return true;
}
