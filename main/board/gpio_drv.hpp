#pragma once
#include "driver/gpio.h"
#include <cstdint>

class GpioDrv {
public:
  static void initOutput(gpio_num_t pin, bool level=false);
  static void setLevel(gpio_num_t pin, bool level);
  static bool getLevel(gpio_num_t pin);
};
