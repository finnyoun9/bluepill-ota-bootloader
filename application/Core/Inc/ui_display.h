#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

/* Initializes every available display. The TFT is independent of I2C. */
bool ui_display_init(bool oled_bus_ready);
void ui_display_clear(void);
void ui_display_show_char(uint8_t line, uint8_t column, char character);
void ui_display_show_string(uint8_t line, uint8_t column, const char *string);
void ui_display_show_num(uint8_t line, uint8_t column, uint32_t number,
                         uint8_t length);

#endif
