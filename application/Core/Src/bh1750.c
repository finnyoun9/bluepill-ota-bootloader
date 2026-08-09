#include "bh1750.h"

#include "env_i2c.h"
#include "stm32f1xx_hal.h"

#define BH1750_ADDRESS             0x23U
#define BH1750_POWER_ON            0x01U
#define BH1750_RESET               0x07U
#define BH1750_CONT_HIGH_RES_MODE  0x10U

static bool bh1750_send_command(uint8_t command) {
    return env_i2c_write(BH1750_ADDRESS, &command, 1U);
}

bool bh1750_init(void) {
    if (!env_i2c_device_ready(BH1750_ADDRESS) ||
        !bh1750_send_command(BH1750_POWER_ON) ||
        !bh1750_send_command(BH1750_RESET) ||
        !bh1750_send_command(BH1750_CONT_HIGH_RES_MODE)) {
        return false;
    }

    HAL_Delay(180U);
    return true;
}

bool bh1750_read_lux(uint16_t *lux) {
    uint8_t data[2];

    if (lux == NULL || !env_i2c_read(BH1750_ADDRESS, data, sizeof(data))) {
        return false;
    }

    const uint16_t raw = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    /* Datasheet conversion: lux = raw / 1.2 = raw * 5 / 6. */
    *lux = (uint16_t)(((uint32_t)raw * 5U + 3U) / 6U);
    return true;
}
