#include "ui_display.h"

#include "oled.h"
#include "tft_st7789.h"

static bool g_oled_ready;
static bool g_tft_ready;

bool ui_display_init(bool oled_bus_ready) {
    g_oled_ready = oled_bus_ready && oled_init();
    g_tft_ready = tft_st7789_init();
    return g_oled_ready || g_tft_ready;
}

void ui_display_clear(void) {
    if (g_oled_ready) {
        oled_clear();
    }
    if (g_tft_ready) {
        tft_st7789_clear();
    }
}

void ui_display_show_char(uint8_t line, uint8_t column, char character) {
    if (g_oled_ready) {
        oled_show_char(line, column, character);
    }
    if (g_tft_ready) {
        tft_st7789_show_char(line, column, character);
    }
}

void ui_display_show_string(uint8_t line, uint8_t column, const char *string) {
    if (g_oled_ready) {
        oled_show_string(line, column, string);
    }
    if (g_tft_ready) {
        tft_st7789_show_string(line, column, string);
    }
}

void ui_display_show_num(uint8_t line, uint8_t column, uint32_t number,
                         uint8_t length) {
    if (g_oled_ready) {
        oled_show_num(line, column, number, length);
    }
    if (g_tft_ready) {
        tft_st7789_show_num(line, column, number, length);
    }
}
