#pragma once
#include "common/singleton.hpp"
#include "modules/rtc/rtc_base.hpp"

class RtcDs1307 : public RtcBase, public Singleton<RtcDs1307> {
  friend class Singleton<RtcDs1307>;
public:
  bool init();

private:
  RtcDs1307() = default;
};
