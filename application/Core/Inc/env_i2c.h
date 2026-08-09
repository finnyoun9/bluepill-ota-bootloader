#ifndef ENV_I2C_H
#define ENV_I2C_H

#include <stdbool.h>
#include <stdint.h>

bool env_i2c_init(void);
bool env_i2c_device_ready(uint8_t address_7bit);
bool env_i2c_write(uint8_t address_7bit, const uint8_t *data, uint16_t length);
bool env_i2c_read(uint8_t address_7bit, uint8_t *data, uint16_t length);

#endif
