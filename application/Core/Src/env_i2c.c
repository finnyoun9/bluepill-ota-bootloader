#include "env_i2c.h"

#include "stm32f1xx_hal.h"

#define ENV_I2C_TIMEOUT_MS  50U

static I2C_HandleTypeDef g_hi2c1;
static bool g_initialized;

bool env_i2c_init(void) {
    g_hi2c1.Instance             = I2C1;
    g_hi2c1.Init.ClockSpeed      = 100000U;
    g_hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    g_hi2c1.Init.OwnAddress1     = 0U;
    g_hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    g_hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    g_hi2c1.Init.OwnAddress2     = 0U;
    g_hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    g_hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

    g_initialized = (HAL_I2C_Init(&g_hi2c1) == HAL_OK);
    return g_initialized;
}

bool env_i2c_device_ready(uint8_t address_7bit) {
    return g_initialized &&
           HAL_I2C_IsDeviceReady(&g_hi2c1, (uint16_t)(address_7bit << 1),
                                 2U, ENV_I2C_TIMEOUT_MS) == HAL_OK;
}

bool env_i2c_write(uint8_t address_7bit, const uint8_t *data, uint16_t length) {
    if (!g_initialized || data == NULL || length == 0U) {
        return false;
    }

    return HAL_I2C_Master_Transmit(&g_hi2c1, (uint16_t)(address_7bit << 1),
                                   (uint8_t *)data, length,
                                   ENV_I2C_TIMEOUT_MS) == HAL_OK;
}

bool env_i2c_read(uint8_t address_7bit, uint8_t *data, uint16_t length) {
    if (!g_initialized || data == NULL || length == 0U) {
        return false;
    }

    return HAL_I2C_Master_Receive(&g_hi2c1, (uint16_t)(address_7bit << 1),
                                  data, length, ENV_I2C_TIMEOUT_MS) == HAL_OK;
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance != I2C1) {
        return;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode  = GPIO_MODE_AF_OD;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
}
