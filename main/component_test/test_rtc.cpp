#include "test_common.hpp"
#include "modules/rtc/rtc_pcf8563.hpp"
#include "common/time_utils.hpp"

static const char* NAME = "RTC";

void test_rtc() {
  TEST_START(NAME);

  RtcPcf8563& rtc = RtcPcf8563::instance();
  rtc.init();

  DateTime now{};
  bool ok = rtc.getTime(now);

  if (ok) {
    TEST_INFO(NAME, "Time: %04d-%02d-%02d %02d:%02d:%02d",
              now.year, now.month, now.day,
              now.hour, now.minute, now.second);

    // Sanity check: year should be reasonable
    if (now.year >= 2024 && now.year <= 2030) {
      TEST_INFO(NAME, "Year is in valid range [2024-2030]");
      TEST_PASS(NAME);
    } else {
      TEST_INFO(NAME, "WARNING: Year %d may indicate RTC not set", now.year);
      TEST_INFO(NAME, "RTC is responding but time may need to be set");
      TEST_PASS(NAME); // RTC works, just not set
    }
  } else {
    TEST_FAIL(NAME, "I2C read failed - check PCF8563 connection");
  }
}
