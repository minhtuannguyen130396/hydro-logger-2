#include "time_utils.hpp"
#include "common/config.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace timeu {

bool isScheduledMinute(int minute) {
  for (int m : cfg::kScheduleMinutes) {
    if (m == minute) return true;
  }
  return false;
}

bool isSyncMinute(int minute) {
  return minute == 0;
}

bool parseServerTime(const char* str, DateTime& out) {
  int h = 0, m = 0, s = 0, d = 0, mon = 0, y = 0;
  if (std::sscanf(str, "%d:%d:%d_%d:%d:%d", &h, &m, &s, &d, &mon, &y) != 6) {
    return false;
  }
  out.hour   = h;
  out.minute = m;
  out.second = s;
  out.day    = d;
  out.month  = mon;
  out.year   = y;
  return true;
}

int64_t toEpochSeconds(const DateTime& dt) {
  struct tm t{};
  t.tm_year = dt.year - 1900;
  t.tm_mon  = dt.month - 1;
  t.tm_mday = dt.day;
  t.tm_hour = dt.hour;
  t.tm_min  = dt.minute;
  t.tm_sec  = dt.second;
  t.tm_isdst = 0;
  return static_cast<int64_t>(mktime(&t));
}

int64_t deltaSeconds(const DateTime& a, const DateTime& b) {
  int64_t ea = toEpochSeconds(a);
  int64_t eb = toEpochSeconds(b);
  int64_t diff = ea - eb;
  return diff < 0 ? -diff : diff;
}

} // namespace timeu
