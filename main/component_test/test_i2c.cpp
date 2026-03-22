#include "test_common.hpp"
#include "board/pins.hpp"
#include "board/i2c_drv.hpp"
#include "driver/i2c.h"

static const char* NAME = "I2C";

void test_i2c_scan() {
  TEST_START(NAME);

  // Init I2C
  TEST_INFO(NAME, "Init I2C%d SCL=GPIO%d SDA=GPIO%d freq=%dHz",
            pins::I2C_NUM, (int)pins::I2C_SCL, (int)pins::I2C_SDA, pins::I2C_FREQ_HZ);

  I2cDrv::init();

  // Scan all addresses (0x08 to 0x77)
  TEST_INFO(NAME, "Scanning I2C bus...");
  int found = 0;

  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin((i2c_port_t)pins::I2C_NUM, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);

    if (err == ESP_OK) {
      const char* name = "";
      if (addr == pins::PCF8563_ADDR) {
        name = " (PCF8563 RTC)";
      }
      TEST_INFO(NAME, "  Found device at 0x%02X%s", addr, name);
      found++;
    }
  }

  if (found > 0) {
    TEST_INFO(NAME, "Total: %d device(s) found", found);
    TEST_PASS(NAME);
  } else {
    TEST_FAIL(NAME, "No I2C devices found - check wiring");
  }
}
