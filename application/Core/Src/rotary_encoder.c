#include "rotary_encoder.h"

#include "stm32f1xx_hal.h"

#define ENCODER_PORT        GPIOA
#define ENCODER_A_PIN       GPIO_PIN_6
#define ENCODER_B_PIN       GPIO_PIN_7
#define ENCODER_BUTTON_PIN  GPIO_PIN_1
#define BUTTON_DEBOUNCE_MS  10U

/* GPIO/EXTI structure follows the Jiangke University encoder example.
 * This EC11 module needs full Gray-code decoding because both contacts
 * can bounce around a detent. Four valid edges equal one detent. */
static const int8_t g_transition_delta[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

static volatile int16_t g_encoder_delta;
static volatile int8_t g_quarter_steps;
static volatile uint8_t g_previous_ab;
static volatile uint8_t g_detent_ab;
static GPIO_PinState g_button_stable = GPIO_PIN_SET;
static GPIO_PinState g_button_sample = GPIO_PIN_SET;
static uint32_t g_button_changed_at;

bool rotary_encoder_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = ENCODER_A_PIN | ENCODER_B_PIN;
    gpio.Mode  = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ENCODER_PORT, &gpio);

    gpio.Pin  = ENCODER_BUTTON_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(ENCODER_PORT, &gpio);

    g_encoder_delta = 0;
    g_quarter_steps = 0;
    g_previous_ab =
        (uint8_t)((HAL_GPIO_ReadPin(ENCODER_PORT, ENCODER_A_PIN) << 1) |
                  HAL_GPIO_ReadPin(ENCODER_PORT, ENCODER_B_PIN));
    g_detent_ab = g_previous_ab;
    g_button_sample = HAL_GPIO_ReadPin(ENCODER_PORT, ENCODER_BUTTON_PIN);
    g_button_stable = g_button_sample;
    g_button_changed_at = HAL_GetTick();

    __HAL_GPIO_EXTI_CLEAR_IT(ENCODER_A_PIN | ENCODER_B_PIN);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
    return true;
}

int16_t rotary_encoder_get_delta(void) {
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const int16_t delta = g_encoder_delta;
    g_encoder_delta = 0;
    if (primask == 0U) {
        __enable_irq();
    }
    return delta;
}

bool rotary_encoder_button_pressed(void) {
    const GPIO_PinState sample = HAL_GPIO_ReadPin(ENCODER_PORT,
                                                   ENCODER_BUTTON_PIN);
    const uint32_t now = HAL_GetTick();

    if (sample != g_button_sample) {
        g_button_sample = sample;
        g_button_changed_at = now;
    } else if ((now - g_button_changed_at) >= BUTTON_DEBOUNCE_MS) {
        g_button_stable = sample;
    }

    return g_button_stable == GPIO_PIN_RESET;
}

void EXTI9_5_IRQHandler(void) {
    const uint16_t pending =
        (uint16_t)(EXTI->PR & (ENCODER_A_PIN | ENCODER_B_PIN));
    if (pending == 0U) {
        return;
    }

    __HAL_GPIO_EXTI_CLEAR_IT(pending);

    const uint8_t current_ab =
        (uint8_t)((HAL_GPIO_ReadPin(ENCODER_PORT, ENCODER_A_PIN) << 1) |
                  HAL_GPIO_ReadPin(ENCODER_PORT, ENCODER_B_PIN));
    const uint8_t previous_ab = g_previous_ab;
    const uint8_t transition =
        (uint8_t)((previous_ab << 2) | current_ab);
    const int8_t edge_delta = g_transition_delta[transition];
    g_previous_ab = current_ab;

    if (current_ab != previous_ab && edge_delta == 0) {
        /* Both bits changed: impossible Gray-code transition, normally
         * caused by contact bounce or a missed edge. Drop this cycle. */
        g_quarter_steps = 0;
        return;
    }

    g_quarter_steps = (int8_t)(g_quarter_steps + edge_delta);

    /* x1 semantics: publish only after a full sequence returns to the
     * detent state sampled at startup. Partial movement and bounce cancel. */
    if (current_ab == g_detent_ab && current_ab != previous_ab) {
        if (g_quarter_steps == 4) {
            g_encoder_delta++;
        } else if (g_quarter_steps == -4) {
            g_encoder_delta--;
        }
        g_quarter_steps = 0;
    }
}
