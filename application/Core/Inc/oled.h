#ifndef OLED_H
#define OLED_H

#include <stdbool.h>
#include <stdint.h>

bool oled_init(void);
void oled_clear(void);
void oled_show_char(uint8_t line, uint8_t column, char character);
void oled_show_string(uint8_t line, uint8_t column, const char *string);
void oled_show_num(uint8_t line, uint8_t column, uint32_t number,
                   uint8_t length);
void oled_show_signed_num(uint8_t line, uint8_t column, int32_t number,
                          uint8_t length);

#endif
