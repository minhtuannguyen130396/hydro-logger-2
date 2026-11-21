#include "modules/io/io_controller.hpp"
#include "board/gpio_drv.hpp"
#include "board/pins.hpp"

void IoController::init() {
  laser_on_ = false;
  dcom_on_ = false;

  // DCOM & Laser chung chan nguon (GPIO 26): idle OFF (HIGH = 1), pull-up enabled
  GpioDrv::initOutput(pins::DCOM_PWR, true);
  if (pins::DCOM_PWR != GPIO_NUM_NC) {
    gpio_set_pull_mode(pins::DCOM_PWR, GPIO_PULLUP_ONLY);
  }

  GpioDrv::initOutput(pins::ULTRA_PWR, false);
  GpioDrv::initOutput(pins::SIM_PWR, false);
  GpioDrv::initOutput(pins::LED, false);
  GpioDrv::initOutput(pins::SPEAKER, false);
}

void IoController::updateSharedPower() {
  // Logic: Luon keo ve 0 (LOW) khi ON, keo ve 1 (HIGH) khi OFF
  const bool any_on = laser_on_ || dcom_on_;
  GpioDrv::setLevel(pins::DCOM_PWR, !any_on);
}

// Laser va Dcom dung chung chan nguon GPIO 26:
// Bat Laser (setLaserPower(true)) se dong thoi bat nguon Dcom (keo chan nguon ve 0).
void IoController::setLaserPower(bool on) {
  laser_on_ = on;
  updateSharedPower();
}

void IoController::setDcomPower(bool on) {
  dcom_on_ = on;
  updateSharedPower();
}
void IoController::setUltrasonicPower(bool on){ GpioDrv::setLevel(pins::ULTRA_PWR, on); }
void IoController::setSimPower(bool on)       { GpioDrv::setLevel(pins::SIM_PWR, !on); }
void IoController::setLed(bool on)            { GpioDrv::setLevel(pins::LED, on); }
void IoController::setSpeaker(bool on)        { GpioDrv::setLevel(pins::SPEAKER, on); }
