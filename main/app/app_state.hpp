#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

class AppState {
public:
  enum Bits : EventBits_t {
    BIT_CONN_OK          = (1 << 0),
    BIT_CONN_FAIL        = (1 << 1),
    BIT_SYNC_RUNNING     = (1 << 2),
    BIT_LAST_SYNC_FAIL   = (1 << 3),
    BIT_LAST_MEASURE_FAIL= (1 << 4),
    BIT_OTA_RUNNING      = (1 << 5),
  };

  bool init() {
    eg_ = xEventGroupCreate();
    return eg_ != nullptr;
  }

  void set(EventBits_t bits)   { if (eg_) xEventGroupSetBits(eg_, bits); }
  void clear(EventBits_t bits) { if (eg_) xEventGroupClearBits(eg_, bits); }
  EventBits_t get() const      { return eg_ ? xEventGroupGetBits(eg_) : 0; }

  bool connOk() const          { return (get() & BIT_CONN_OK) != 0; }
  bool connFail() const        { return (get() & BIT_CONN_FAIL) != 0; }
  bool lastSyncFail() const    { return (get() & BIT_LAST_SYNC_FAIL) != 0; }

private:
  EventGroupHandle_t eg_{nullptr};
};
