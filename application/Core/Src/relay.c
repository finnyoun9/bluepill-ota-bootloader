#include "relay.h"

#include "stm32f1xx_hal.h"

#define RELAY_PORT    GPIOA
#define RELAY1_PIN    GPIO_PIN_2
#define RELAY2_PIN    GPIO_PIN_3

/* Most relay modules are active-LOW: IN low closes the contact (load ON).
 * If the module is active-high, set to 0 or flip the on-board jumper. */
#define RELAY_ACTIVE_LOW  1U

static const uint16_t k_relay_pins[RELAY_CHANNELS] = { RELAY1_PIN,
                                                       RELAY2_PIN };
static bool g_relay_state[RELAY_CHANNELS];
static bool g_auto_mode;

static GPIO_PinState relay_io_level(bool on) {
#if RELAY_ACTIVE_LOW
    return on ? GPIO_PIN_RESET : GPIO_PIN_SET;
#else
    return on ? GPIO_PIN_SET : GPIO_PIN_RESET;
#endif
}

bool relay_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* Drive the inactive level before enabling push-pull output mode. */
    HAL_GPIO_WritePin(RELAY_PORT, RELAY1_PIN | RELAY2_PIN, GPIO_PIN_SET);

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = RELAY1_PIN | RELAY2_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RELAY_PORT, &gpio);

    g_relay_state[0U] = false;
    g_relay_state[1U] = false;
    g_auto_mode = false;
    return true;
}

void relay_set(uint8_t channel, bool on) {
    if (channel >= RELAY_CHANNELS) {
        return;
    }
    g_relay_state[channel] = on;
    HAL_GPIO_WritePin(RELAY_PORT, k_relay_pins[channel], relay_io_level(on));
}

bool relay_get_state(uint8_t channel) {
    if (channel >= RELAY_CHANNELS) {
        return false;
    }
    return g_relay_state[channel];
}

bool relay_auto_enabled(void) {
    return g_auto_mode;
}

void relay_set_auto(bool enabled) {
    g_auto_mode = enabled;
}
