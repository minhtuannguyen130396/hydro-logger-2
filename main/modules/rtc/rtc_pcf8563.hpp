#pragma once
#include "common/singleton.hpp"
#include "modules/rtc/rtc_base.hpp"

class RtcPcf8563 : public RtcBase, public Singleton<RtcPcf8563> {
  friend class Singleton<RtcPcf8563>;
public:
  bool init();

  // Timer interrupt for periodic wakeup (e.g. every 10 seconds)
  bool enableTimerInterrupt(uint8_t seconds);
  bool disableTimerInterrupt();
  bool clearTimerFlag();
  bool isTimerTriggered(bool& triggered);

private:
  RtcPcf8563() = default;
};
