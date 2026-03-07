#pragma once
#include "common/time_utils.hpp"

class RtcBase {
public:
  bool getTime(DateTime& out) const;
  bool setTime(const DateTime& in) const;
};
