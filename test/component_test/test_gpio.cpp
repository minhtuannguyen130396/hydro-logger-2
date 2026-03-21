#include "test_common.hpp"
#include "board/pins.hpp"
#include "modules/io/io_controller.hpp"

static const char* NAME = "GPIO";

void test_gpio() {
  TEST_START(NAME);
  IoController& io = IoController::instance();

  // Test LED
  TEST_INFO(NAME, "LED blink test (5 times, 500ms interval)");
  for (int i = 0; i < 5; i++) {
    io.setLed(true);
    TEST_INFO(NAME, "  LED ON  [%d/5]", i + 1);
    testDelayMs(500);
    io.setLed(false);
    TEST_INFO(NAME, "  LED OFF [%d/5]", i + 1);
    testDelayMs(500);
  }

  // Test power pins - toggle each one briefly
  TEST_INFO(NAME, "--- Power pin toggle test ---");

  TEST_INFO(NAME, "LASER_PWR (GPIO%d) HIGH", (int)pins::LASER_PWR);
  io.setLaserPower(true);
  testDelayMs(500);
  TEST_INFO(NAME, "LASER_PWR LOW");
  io.setLaserPower(false);
  testDelayMs(200);
  io.setLaserPower(true); // return to idle HIGH
  TEST_INFO(NAME, "LASER_PWR idle HIGH");

  TEST_INFO(NAME, "ULTRA_PWR (GPIO%d) HIGH", (int)pins::ULTRA_PWR);
  io.setUltrasonicPower(true);
  testDelayMs(500);
  io.setUltrasonicPower(false);
  TEST_INFO(NAME, "ULTRA_PWR idle LOW");

  TEST_INFO(NAME, "SIM_PWR (GPIO%d) LOW (pulse)", (int)pins::SIM_PWR);
  io.setSimPower(false); // active edge
  testDelayMs(200);
  io.setSimPower(true);  // idle HIGH
  TEST_INFO(NAME, "SIM_PWR idle HIGH");

  TEST_INFO(NAME, "DCOM_PWR (GPIO%d) HIGH", (int)pins::DCOM_PWR);
  io.setDcomPower(true);
  testDelayMs(500);
  io.setDcomPower(false);
  TEST_INFO(NAME, "DCOM_PWR idle LOW");

  TEST_INFO(NAME, "All power pins returned to idle state");
  TEST_PASS(NAME);
}
