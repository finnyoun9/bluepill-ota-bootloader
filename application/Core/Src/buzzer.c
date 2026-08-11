#include "buzzer.h"

#include "stm32f1xx_hal.h"

#define BUZZER_PORT  GPIOB
#define BUZZER_PIN   GPIO_PIN_1

static bool g_buzzer_on;

bool buzzer_init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Set the inactive level before switching the pin to output mode. */
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN, GPIO_PIN_SET);

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = BUZZER_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BUZZER_PORT, &gpio);

    g_buzzer_on = false;
    return true;
}

void buzzer_set(bool on) {
    g_buzzer_on = on;
    HAL_GPIO_WritePin(BUZZER_PORT, BUZZER_PIN,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

bool buzzer_get_state(void) {
    return g_buzzer_on;
}
