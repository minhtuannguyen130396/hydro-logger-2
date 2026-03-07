#include "modules/rtc/rtc_ds1307.hpp"
#include "board/i2c_drv.hpp"
#include "board/pins.hpp"

#include "esp_log.h"

static const char* TAG = "RtcDs1307";

static uint8_t bcd2bin(uint8_t v) { return (v & 0x0F) + ((v >> 4) * 10); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

bool RtcDs1307::init() {
  bool ok = I2cDrv::init();
  ESP_LOGI(TAG, "i2c init: %d", (int)ok);
  return ok;
}

bool RtcBase::getTime(DateTime& out) const {
  uint8_t buf[7]{};
  if (!I2cDrv::readReg(pins::DS1307_ADDR, 0x00, buf, sizeof(buf))) return false;
  out.second = bcd2bin(buf[0] & 0x7F);
  out.minute = bcd2bin(buf[1] & 0x7F);
  out.hour   = bcd2bin(buf[2] & 0x3F);
  out.day    = bcd2bin(buf[4] & 0x3F);
  out.month  = bcd2bin(buf[5] & 0x1F);
  out.year   = 2000 + bcd2bin(buf[6]);
  return true;
}

bool RtcBase::setTime(const DateTime& in) const {
  uint8_t buf[7]{};
  buf[0] = bin2bcd((uint8_t)in.second);
  buf[1] = bin2bcd((uint8_t)in.minute);
  buf[2] = bin2bcd((uint8_t)in.hour);
  buf[3] = 1; // day of week (not used)
  buf[4] = bin2bcd((uint8_t)in.day);
  buf[5] = bin2bcd((uint8_t)in.month);
  buf[6] = bin2bcd((uint8_t)(in.year % 100));
  return I2cDrv::writeReg(pins::DS1307_ADDR, 0x00, buf, sizeof(buf));
}
