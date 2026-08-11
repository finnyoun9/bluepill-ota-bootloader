#include "ws2812b.h"

#include "stm32f1xx_hal.h"

/*
 * Hardware-verified WS2812B output for this Blue Pill:
 *   PB5 -> strip DIN
 *   HSE 8MHz -> PLL x8 -> SYSCLK 64MHz, FLASH_LATENCY_2
 *
 * The delay counts come from the previously proven ws2812b-stm32 project.
 * This project was additionally checked end-to-end with a logic analyzer on
 * the first LED's DOUT. Keep this implementation clock-dependent on purpose:
 * changing SYSCLK or Flash latency requires re-measuring the waveform.
 */
#define WS_DATA_PIN  GPIO_PIN_5

#define NOP10()      __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
                     __NOP(); __NOP(); __NOP(); __NOP(); __NOP()

static bool g_ws_ready;

static void ws_send_byte(uint8_t byte) {
    for (int8_t bit = 7; bit >= 0; bit--) {
        GPIOB->BSRR = WS_DATA_PIN;

        if ((byte & (uint8_t)(1U << bit)) != 0U) {
            /* Logic 1: about 0.8 us high, then about 0.3 us low. */
            NOP10(); NOP10(); NOP10(); NOP10(); NOP10(); NOP10();
            GPIOB->BRR = WS_DATA_PIN;
            NOP10(); NOP10();
        } else {
            /* Logic 0: about 0.4 us high, then about 0.8 us low. */
            NOP10();
            __NOP(); __NOP(); __NOP(); __NOP(); __NOP();
            GPIOB->BRR = WS_DATA_PIN;
            NOP10(); NOP10(); NOP10(); NOP10(); NOP10();
        }
    }
}

static bool ws_send_white(uint8_t brightness) {
    if (!g_ws_ready) {
        return false;
    }

    /*
     * FreeRTOS/UART interrupts would stretch individual bits beyond the
     * WS2812B tolerance. A full 15-LED frame occupies about 0.5 ms, so the
     * application sends only when brightness changes.
     */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    for (uint8_t led = 0U; led < WS2812B_LED_COUNT; led++) {
        ws_send_byte(brightness); /* Green */
        ws_send_byte(brightness); /* Red */
        ws_send_byte(brightness); /* Blue */
    }

    GPIOB->BRR = WS_DATA_PIN;
    for (volatile uint32_t reset = 0U; reset < 3500U; reset++) {
        __NOP();
    }

    if (primask == 0U) {
        __enable_irq();
    }
    return true;
}

bool ws2812b_init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = WS_DATA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
    GPIOB->BRR = WS_DATA_PIN;

    g_ws_ready = true;
    return ws_send_white(0U);
}

bool ws2812b_show_white(uint8_t brightness) {
    return ws_send_white(brightness);
}

bool ws2812b_clear(void) {
    return ws_send_white(0U);
}
