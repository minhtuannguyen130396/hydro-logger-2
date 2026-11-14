#pragma once
#include "driver/gpio.h"

enum class WakeReason {
  RtcAlarm,   // Woken by RTC INT (EXT0 / GPIO)
  Timeout,    // Woken by light-sleep timer expiry
  Unknown,
};

class PowerManager {
public:
  void enterSafeMode();
  void exitSafeMode();

  // Fully tear down Wi-Fi (disconnect + stop + deinit) so the radio is
  // completely off and all Wi-Fi resources are freed before sleeping.
  void shutdownWifi();

  // Configure EXT0 wakeup on wake_pin (active-low, RTC INT) and enter deep
  // sleep. Call enterSafeMode() first to power down peripherals. Never returns
  // — the chip resets through app_main on the next wake.
  void enterDeepSleep(gpio_num_t wake_pin);

  // Enter light sleep with two wakeup sources:
  //   1. EXT0 on wake_pin (RTC INT active-low)
  //   2. Timer of timeout_ms milliseconds
  // Returns the reason the device woke up.
  WakeReason enterLightSleep(gpio_num_t wake_pin, uint32_t timeout_ms);

private:
  bool in_safe_mode_{false};
};
