#include "i2c_port.h"
#include "i2c_drv.hpp"

/*
 * Adapter: maps the C-style i2c_port API (used by pcf8563 lib)
 * onto the project's I2cDrv static methods.
 *
 * pcf8563 calls i2c_port_write(addr, buf, len) where buf[0] = register,
 * buf[1..] = data.  I2cDrv::writeReg already expects (addr, reg, data, len).
 */

extern "C" bool i2c_port_write(uint8_t addr_7bit, const uint8_t *data, size_t len) {
  if (!data || len < 1) return false;
  uint8_t reg = data[0];
  if (len == 1) {
    // register-only write (no payload)
    return I2cDrv::writeReg(addr_7bit, reg, nullptr, 0);
  }
  return I2cDrv::writeReg(addr_7bit, reg, data + 1, (uint8_t)(len - 1));
}

extern "C" bool i2c_port_write_read(uint8_t addr_7bit,
                                     const uint8_t *wr_data, size_t wr_len,
                                     uint8_t *rd_data, size_t rd_len) {
  if (!wr_data || wr_len < 1 || !rd_data || rd_len == 0) return false;
  uint8_t reg = wr_data[0];
  return I2cDrv::readReg(addr_7bit, reg, rd_data, (uint8_t)rd_len);
}
