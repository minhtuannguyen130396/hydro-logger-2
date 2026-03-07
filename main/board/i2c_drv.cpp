#include "i2c_drv.hpp"
#include "board/pins.hpp"

#include "driver/i2c.h"

bool I2cDrv::init() {
  i2c_config_t conf{};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = pins::I2C_SDA;
  conf.scl_io_num = pins::I2C_SCL;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = pins::I2C_FREQ_HZ;
  if (i2c_param_config((i2c_port_t)pins::I2C_NUM, &conf) != ESP_OK) return false;
  if (i2c_driver_install((i2c_port_t)pins::I2C_NUM, conf.mode, 0, 0, 0) != ESP_OK) return false;
  return true;
}

bool I2cDrv::writeReg(uint8_t addr, uint8_t reg, const uint8_t* data, uint8_t len) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  if (len && data) i2c_master_write(cmd, (uint8_t*)data, len, true);
  i2c_master_stop(cmd);
  esp_err_t err = i2c_master_cmd_begin((i2c_port_t)pins::I2C_NUM, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return err == ESP_OK;
}

bool I2cDrv::readReg(uint8_t addr, uint8_t reg, uint8_t* data, uint8_t len) {
  if (!data || !len) return false;
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
  if (len > 1) i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
  i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
  i2c_master_stop(cmd);
  esp_err_t err = i2c_master_cmd_begin((i2c_port_t)pins::I2C_NUM, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return err == ESP_OK;
}
