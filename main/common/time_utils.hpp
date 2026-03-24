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

  // Parse server time string "HH:MM:SS_DD:MM:YYYY" into DateTime
  bool parseServerTime(const char* str, DateTime& out);

  // Convert DateTime to epoch seconds (since 1970-01-01)
  int64_t toEpochSeconds(const DateTime& dt);

  // Absolute difference in seconds between two DateTimes
  int64_t deltaSeconds(const DateTime& a, const DateTime& b);
}
