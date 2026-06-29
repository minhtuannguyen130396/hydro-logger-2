#pragma once
#include "driver/gpio.h"

class PowerManager {
public:
  void enterSafeMode();
  void exitSafeMode();

  // Configure EXT0 wakeup on wake_pin (active-low, RTC INT) and enter deep
  // sleep. Call enterSafeMode() first to power down peripherals. Never returns
  // — the chip resets through app_main on the next wake.
  void enterDeepSleep(gpio_num_t wake_pin);

private:
  bool in_safe_mode_{false};
};
