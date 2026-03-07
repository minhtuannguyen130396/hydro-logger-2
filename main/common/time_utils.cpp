#include "time_utils.hpp"
#include "common/config.hpp"

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

} // namespace timeu
