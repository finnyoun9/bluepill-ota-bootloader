#ifndef TFT_ST7789_H
#define TFT_ST7789_H

#include <stdbool.h>
#include <stdint.h>

/* GMT020-02: 2.0-inch ST7789, 240x320, 4-wire SPI. */
bool tft_st7789_init(void);
void tft_st7789_clear(void);
void tft_st7789_show_char(uint8_t line, uint8_t column, char character);
void tft_st7789_show_string(uint8_t line, uint8_t column,
                            const char *string);
void tft_st7789_show_num(uint8_t line, uint8_t column, uint32_t number,
                         uint8_t length);

#endif
