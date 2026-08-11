#ifndef WS2812B_H
#define WS2812B_H

#include <stdbool.h>
#include <stdint.h>

#define WS2812B_LED_COUNT  15U

bool ws2812b_init(void);
bool ws2812b_show_white(uint8_t brightness);
bool ws2812b_clear(void);

#endif
