#ifndef TFT_CHINESE_FONT_H
#define TFT_CHINESE_FONT_H

#include <stdbool.h>
#include <stdint.h>

#define TFT_CHINESE_GLYPH_WIDTH   16U
#define TFT_CHINESE_GLYPH_HEIGHT  16U

/* Returns a compact 16x16 monochrome glyph for the UI's UTF-8 subset. */
const uint8_t *tft_chinese_font_find(uint16_t codepoint);

#endif
