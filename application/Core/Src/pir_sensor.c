#include "pir_sensor.h"

#include "stm32f1xx_hal.h"

#define PIR_PORT  GPIOB
#define PIR_PIN   GPIO_PIN_0

bool pir_sensor_init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = PIR_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PIR_PORT, &gpio);
    return true;
}

bool pir_sensor_motion_detected(void) {
    return HAL_GPIO_ReadPin(PIR_PORT, PIR_PIN) == GPIO_PIN_SET;
}
