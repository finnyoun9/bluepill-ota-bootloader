#ifndef TFT_ST7789_H
#define TFT_ST7789_H

#include <stdbool.h>
#include <stdint.h>

/* GMT020-02: 2.0-inch ST7789, 240x320, 4-wire SPI. */
bool tft_st7789_init(void);
void tft_st7789_clear(void);
void tft_st7789_fill_rect(uint16_t x, uint16_t y, uint16_t width,
                          uint16_t height, uint16_t color);
void tft_st7789_draw_char(uint16_t x, uint16_t y, char character,
                          uint16_t foreground, uint16_t background,
                          uint8_t scale);
void tft_st7789_draw_text(uint16_t x, uint16_t y, const char *string,
                          uint16_t foreground, uint16_t background,
                          uint8_t scale);
void tft_st7789_draw_utf8(uint16_t x, uint16_t y, const char *string,
                          uint16_t foreground, uint16_t background,
                          uint8_t scale);

#endif
