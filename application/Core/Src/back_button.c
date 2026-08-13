#include "back_button.h"

#include "stm32f1xx_hal.h"

#define BACK_BUTTON_PORT         GPIOA
#define BACK_BUTTON_PIN          GPIO_PIN_4
#define BACK_BUTTON_DEBOUNCE_MS  15U

static GPIO_PinState g_stable = GPIO_PIN_SET;
static GPIO_PinState g_sample = GPIO_PIN_SET;
static uint32_t g_changed_at;

bool back_button_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = BACK_BUTTON_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BACK_BUTTON_PORT, &gpio);

    g_sample = HAL_GPIO_ReadPin(BACK_BUTTON_PORT, BACK_BUTTON_PIN);
    g_stable = g_sample;
    g_changed_at = HAL_GetTick();
    return true;
}

bool back_button_pressed(void) {
    const GPIO_PinState sample =
        HAL_GPIO_ReadPin(BACK_BUTTON_PORT, BACK_BUTTON_PIN);
    const uint32_t now = HAL_GetTick();

    if (sample != g_sample) {
        g_sample = sample;
        g_changed_at = now;
    } else if ((now - g_changed_at) >= BACK_BUTTON_DEBOUNCE_MS) {
        g_stable = sample;
    }

    return g_stable == GPIO_PIN_RESET;
}
