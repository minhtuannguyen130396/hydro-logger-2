#include "modules/rtc/rtc_pcf8563.hpp"
#include "modules/rtc/pcf8563.h"
#include "board/i2c_drv.hpp"

#include "esp_log.h"

static const char* TAG = "RtcPcf8563";

bool RtcPcf8563::init() {
  bool ok = I2cDrv::init();
  ESP_LOGI(TAG, "i2c init: %d", (int)ok);
  if (!ok) return false;

  PCF8563_Status st = pcf8563_init();
  if (st == PCF8563_TIME_INVALID) {
    ESP_LOGW(TAG, "voltage low detected, time invalid (battery replaced?)");
    // Not fatal — time can be set later
  } else if (st != PCF8563_OK) {
    ESP_LOGE(TAG, "pcf8563_init failed: %d", (int)st);
    return false;
  }

  ESP_LOGI(TAG, "PCF8563 init OK");
  return true;
}

// ---------- RtcBase interface (getTime / setTime) ----------

bool RtcBase::getTime(DateTime& out) const {
  PCF8563_DateTime dt{};
  if (pcf8563_getDateTime(&dt) != PCF8563_OK) return false;

  out.second = dt.time.second;
  out.minute = dt.time.minute;
  out.hour   = dt.time.hour;
  out.day    = dt.date.day;
  out.month  = dt.date.month;
  out.year   = dt.date.year;
  return true;
}

bool RtcBase::setTime(const DateTime& in) const {
  PCF8563_DateTime dt{};
  dt.time.second = (uint8_t)in.second;
  dt.time.minute = (uint8_t)in.minute;
  dt.time.hour   = (uint8_t)in.hour;
  dt.date.day    = (uint8_t)in.day;
  dt.date.weekday = 0;
  dt.date.month  = (uint8_t)in.month;
  dt.date.year   = (uint16_t)in.year;
  return pcf8563_setDateTime(&dt) == PCF8563_OK;
}

// ---------- Timer interrupt API ----------

bool RtcPcf8563::enableTimerInterrupt(uint8_t seconds) {
  if (seconds == 0) return false;

  PCF8563_TimerConfig cfg{};
  cfg.clock = PCF8563_TIMER_CLK_1HZ;  // 1Hz source → value = seconds
  cfg.value = seconds;
  cfg.interrupt_enable = true;
  cfg.pulse_interrupt  = false;        // level interrupt (held until cleared)

  PCF8563_Status st = pcf8563_setTimer(&cfg);
  if (st != PCF8563_OK) {
    ESP_LOGE(TAG, "enableTimerInterrupt failed: %d", (int)st);
    return false;
  }
  ESP_LOGI(TAG, "timer interrupt enabled: %u sec", (unsigned)seconds);
  return true;
}

bool RtcPcf8563::disableTimerInterrupt() {
  PCF8563_Status st = pcf8563_disableTimer();
  if (st != PCF8563_OK) {
    ESP_LOGE(TAG, "disableTimerInterrupt failed: %d", (int)st);
    return false;
  }
  return true;
}

bool RtcPcf8563::clearTimerFlag() {
  // Read CS2, clear TF, preserve other bits
  // pcf8563 lib doesn't expose a dedicated clearTimerFlag,
  // so we re-read and rewrite CS2 manually via the alarm flag helper pattern.
  // However, the simplest approach: disable then re-enable timer.
  // But that resets the countdown. Instead, use the low-level approach:
  uint8_t cs2 = 0;
  PCF8563_Status st;

  // Read current CS2
  // We can access via pcf8563_isAlarmTriggered as a proxy to verify I2C works,
  // but let's just re-set timer with same config (TF is cleared on write of CS2
  // when TF bit is written as 0).
  // Actually pcf8563_setTimer already clears TF. So just call it again.
  // But we don't store config... simpler: the lib's setTimer clears TF internally.

  // For a clean approach, expose a minimal write. Since pcf8563 lib's
  // pcf8563_write_cs2 is static, we replicate the logic here.
  // Read CS2:
  bool triggered = false;
  st = pcf8563_isAlarmTriggered(&triggered); // just to test I2C
  (void)triggered;
  if (st != PCF8563_OK) return false;

  // The PCF8563 TF flag clears when CS2 is written with TF=0.
  // pcf8563_setTimer writes CS2 with TF=0, so re-enabling does the trick.
  // But if timer is already running we don't want to reset countdown.
  // Safest: read timer_control to get current config, then just rewrite CS2.

  // Direct I2C approach via I2cDrv:
  uint8_t addr = 0x51;
  if (!I2cDrv::readReg(addr, 0x01, &cs2, 1)) return false;

  // Clear TF (bit 2), keep everything else
  cs2 &= ~(1u << 2);
  return I2cDrv::writeReg(addr, 0x01, &cs2, 1);
}

bool RtcPcf8563::isTimerTriggered(bool& triggered) {
  // Read CS2 register, check TF bit
  uint8_t cs2 = 0;
  uint8_t addr = 0x51;
  if (!I2cDrv::readReg(addr, 0x01, &cs2, 1)) return false;
  triggered = (cs2 & (1u << 2)) != 0;
  return true;
}
