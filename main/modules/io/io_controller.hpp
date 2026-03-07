#pragma once
#include "common/singleton.hpp"
#include "driver/gpio.h"

class IoController : public Singleton<IoController> {
  friend class Singleton<IoController>;
public:
  void init();

  void setLaserPower(bool on);
  void setUltrasonicPower(bool on);
  void setSimPower(bool on);
  void setDcomPower(bool on);

  void setLed(bool on);
  void setSpeaker(bool on);

private:
  IoController() = default;
};
