#include "modules/io/io_controller.hpp"
#include "board/gpio_drv.hpp"
#include "board/pins.hpp"

void IoController::init() {
  GpioDrv::initOutput(pins::LASER_PWR, false);
  GpioDrv::initOutput(pins::ULTRA_PWR, false);
  GpioDrv::initOutput(pins::SIM_PWR, false);
  GpioDrv::initOutput(pins::DCOM_PWR, false);
  GpioDrv::initOutput(pins::LED, false);
  GpioDrv::initOutput(pins::SPEAKER, false);
}

void IoController::setLaserPower(bool on)     { GpioDrv::setLevel(pins::LASER_PWR, on); }
void IoController::setUltrasonicPower(bool on){ GpioDrv::setLevel(pins::ULTRA_PWR, on); }
void IoController::setSimPower(bool on)       { GpioDrv::setLevel(pins::SIM_PWR, on); }
void IoController::setDcomPower(bool on)      { GpioDrv::setLevel(pins::DCOM_PWR, on); }
void IoController::setLed(bool on)            { GpioDrv::setLevel(pins::LED, on); }
void IoController::setSpeaker(bool on)        { GpioDrv::setLevel(pins::SPEAKER, on); }
