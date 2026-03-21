#include "test_common.hpp"
#include "board/adc_drv.hpp"

static const char* NAME = "ADC";

void test_adc() {
  TEST_START(NAME);

  // Read multiple samples
  int total = 0;
  const int samples = 5;

  for (int i = 0; i < samples; i++) {
    int mv = AdcDrv::readMilliVolts();
    TEST_INFO(NAME, "Sample %d: %d mV", i + 1, mv);
    total += mv;
    testDelayMs(200);
  }

  int avg = total / samples;
  TEST_INFO(NAME, "Average: %d mV (%d samples)", avg, samples);

  if (avg > 0) {
    TEST_PASS(NAME);
  } else {
    TEST_FAIL(NAME, "All readings are 0 - check ADC wiring");
  }
}
