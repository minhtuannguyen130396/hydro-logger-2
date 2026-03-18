#ifndef I2C_PORT_H
#define I2C_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool i2c_port_write(uint8_t addr_7bit, const uint8_t *data, size_t len);
bool i2c_port_write_read(uint8_t addr_7bit,
                         const uint8_t *wr_data, size_t wr_len,
                         uint8_t *rd_data, size_t rd_len);

#ifdef __cplusplus
}
#endif

#endif
