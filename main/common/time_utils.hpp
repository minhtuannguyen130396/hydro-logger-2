#pragma once
#include <cstdint>

struct DateTime {
  int year{2026};
  int month{1};
  int day{1};
  int hour{0};
  int minute{0};
  int second{0};
};

namespace timeu {
  bool isScheduledMinute(int minute);
  bool isSyncMinute(int minute); // minute == 0
}
