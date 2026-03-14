#include "gpio_drv.hpp"

void GpioDrv::initOutput(gpio_num_t pin, bool level) {
  if (pin == GPIO_NUM_NC) return;
  gpio_config_t cfg{};
  cfg.intr_type = GPIO_INTR_DISABLE;
  cfg.mode = GPIO_MODE_OUTPUT;
  cfg.pin_bit_mask = (1ULL << pin);
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&cfg);
  gpio_set_level(pin, level ? 1 : 0);
}

void GpioDrv::setLevel(gpio_num_t pin, bool level) {
  if (pin == GPIO_NUM_NC) return;
  gpio_set_level(pin, level ? 1 : 0);
}

bool GpioDrv::getLevel(gpio_num_t pin) {
  if (pin == GPIO_NUM_NC) return false;
  return gpio_get_level(pin) != 0;
}
