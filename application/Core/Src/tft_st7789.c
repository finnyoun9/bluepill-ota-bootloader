#include "tft_st7789.h"

#include "oled_font.h"
#include "tft_chinese_font.h"
#include "stm32f1xx_hal.h"

#define TFT_WIDTH             240U
#define TFT_HEIGHT            320U
#define TFT_FONT_WIDTH        8U
#define TFT_FONT_HEIGHT       16U

#define TFT_CS_PORT           GPIOB
#define TFT_CS_PIN            GPIO_PIN_12
#define TFT_SCK_PIN           GPIO_PIN_13
#define TFT_DC_PORT           GPIOB
#define TFT_DC_PIN            GPIO_PIN_14
#define TFT_MOSI_PIN          GPIO_PIN_15
#define TFT_RST_PORT          GPIOA
#define TFT_RST_PIN           GPIO_PIN_8

#define TFT_COLOR_BACKGROUND  0x0008U

#define ST7789_SWRESET        0x01U
#define ST7789_SLPOUT         0x11U
#define ST7789_NORON          0x13U
#define ST7789_INVON          0x21U
#define ST7789_DISPON         0x29U
#define ST7789_CASET          0x2AU
#define ST7789_RASET          0x2BU
#define ST7789_RAMWR          0x2CU
#define ST7789_MADCTL         0x36U
#define ST7789_COLMOD         0x3AU

static SPI_HandleTypeDef g_tft_spi;
static bool g_tft_initialized;

static void tft_select(void) {
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_RESET);
}

static void tft_deselect(void) {
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_SET);
}

static bool tft_transmit(const uint8_t *data, uint16_t length) {
    return HAL_SPI_Transmit(&g_tft_spi, (uint8_t *)data, length, 100U) ==
           HAL_OK;
}

static bool tft_write_command(uint8_t command) {
    HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_RESET);
    tft_select();
    const bool ok = tft_transmit(&command, 1U);
    tft_deselect();
    return ok;
}

static bool tft_write_data(const uint8_t *data, uint16_t length) {
    HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_SET);
    tft_select();
    const bool ok = tft_transmit(data, length);
    tft_deselect();
    return ok;
}

static bool tft_write_command_data(uint8_t command, const uint8_t *data,
                                   uint16_t length) {
    return tft_write_command(command) && tft_write_data(data, length);
}

static bool tft_set_window(uint16_t x_start, uint16_t y_start,
                           uint16_t x_end, uint16_t y_end) {
    const uint8_t column[] = {
        (uint8_t)(x_start >> 8), (uint8_t)x_start,
        (uint8_t)(x_end >> 8), (uint8_t)x_end
    };
    const uint8_t row[] = {
        (uint8_t)(y_start >> 8), (uint8_t)y_start,
        (uint8_t)(y_end >> 8), (uint8_t)y_end
    };

    return tft_write_command_data(ST7789_CASET, column, sizeof(column)) &&
           tft_write_command_data(ST7789_RASET, row, sizeof(row)) &&
           tft_write_command(ST7789_RAMWR);
}

void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi) {
    if (hspi->Instance != SPI2) {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = TFT_SCK_PIN | TFT_MOSI_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = TFT_CS_PIN | TFT_DC_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin = TFT_RST_PIN;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_SET);
}

bool tft_st7789_init(void) {
    g_tft_spi.Instance = SPI2;
    g_tft_spi.Init.Mode = SPI_MODE_MASTER;
    g_tft_spi.Init.Direction = SPI_DIRECTION_2LINES;
    g_tft_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    g_tft_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    g_tft_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    g_tft_spi.Init.NSS = SPI_NSS_SOFT;
    g_tft_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    g_tft_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    g_tft_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    g_tft_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    g_tft_spi.Init.CRCPolynomial = 7U;

    if (HAL_SPI_Init(&g_tft_spi) != HAL_OK) {
        return false;
    }

    HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(120U);

    if (!tft_write_command(ST7789_SWRESET)) {
        return false;
    }
    HAL_Delay(150U);
    if (!tft_write_command(ST7789_SLPOUT)) {
        return false;
    }
    HAL_Delay(120U);

    const uint8_t color_mode = 0x55U; /* RGB565 */
    const uint8_t orientation = 0x00U; /* Adafruit rotation 2: portrait 180deg. */
    const uint8_t column[] = {0x00U, 0x00U, 0x00U, 0xEFU};
    const uint8_t row[] = {0x00U, 0x00U, 0x01U, 0x3FU};
    if (!tft_write_command_data(ST7789_COLMOD, &color_mode, 1U)) {
        return false;
    }
    HAL_Delay(10U);
    if (!tft_write_command_data(ST7789_MADCTL, &orientation, 1U) ||
        !tft_write_command_data(ST7789_CASET, column, sizeof(column)) ||
        !tft_write_command_data(ST7789_RASET, row, sizeof(row)) ||
        !tft_write_command(ST7789_INVON)) {
        return false;
    }
    HAL_Delay(10U);
    if (!tft_write_command(ST7789_NORON)) {
        return false;
    }
    HAL_Delay(10U);
    if (!tft_write_command(ST7789_DISPON)) {
        return false;
    }

    HAL_Delay(20U);
    g_tft_initialized = true;
    tft_st7789_clear();
    return true;
}

void tft_st7789_clear(void) {
    tft_st7789_fill_rect(0U, 0U, TFT_WIDTH, TFT_HEIGHT,
                         TFT_COLOR_BACKGROUND);
}

void tft_st7789_fill_rect(uint16_t x, uint16_t y, uint16_t width,
                          uint16_t height, uint16_t color) {
    if (!g_tft_initialized || width == 0U || height == 0U ||
        x >= TFT_WIDTH || y >= TFT_HEIGHT) {
        return;
    }
    if ((uint32_t)x + width > TFT_WIDTH) {
        width = (uint16_t)(TFT_WIDTH - x);
    }
    if ((uint32_t)y + height > TFT_HEIGHT) {
        height = (uint16_t)(TFT_HEIGHT - y);
    }
    if (!tft_set_window(x, y, (uint16_t)(x + width - 1U),
                        (uint16_t)(y + height - 1U))) {
        return;
    }

    uint8_t pixels[128U];
    uint32_t bytes_remaining = (uint32_t)width * height * 2U;

    for (uint16_t i = 0U; i < sizeof(pixels); i += 2U) {
        pixels[i] = (uint8_t)(color >> 8);
        pixels[i + 1U] = (uint8_t)color;
    }

    HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_SET);
    tft_select();
    while (bytes_remaining > 0U) {
        const uint16_t chunk = bytes_remaining > sizeof(pixels)
                                   ? (uint16_t)sizeof(pixels)
                                   : (uint16_t)bytes_remaining;
        if (!tft_transmit(pixels, chunk)) {
            break;
        }
        bytes_remaining -= chunk;
    }
    tft_deselect();
}

void tft_st7789_draw_char(uint16_t x, uint16_t y, char character,
                          uint16_t foreground, uint16_t background,
                          uint8_t scale) {
    if (!g_tft_initialized || (scale != 1U && scale != 2U)) {
        return;
    }
    if (character < ' ' || character > '~') {
        character = '?';
    }

    const uint16_t width = (uint16_t)(TFT_FONT_WIDTH * scale);
    const uint16_t height = (uint16_t)(TFT_FONT_HEIGHT * scale);
    if ((uint32_t)x + width > TFT_WIDTH ||
        (uint32_t)y + height > TFT_HEIGHT ||
        !tft_set_window(x, y, (uint16_t)(x + width - 1U),
                        (uint16_t)(y + height - 1U))) {
        return;
    }

    const uint8_t *glyph = &OLED_F8x16[(uint8_t)(character - ' ') * 16U];
    uint8_t row_pixels[TFT_FONT_WIDTH * 2U * 2U];

    HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_SET);
    tft_select();
    for (uint8_t source_y = 0U; source_y < TFT_FONT_HEIGHT; source_y++) {
        for (uint8_t source_x = 0U; source_x < TFT_FONT_WIDTH; source_x++) {
            const uint8_t font_byte =
                glyph[(source_y / 8U) * TFT_FONT_WIDTH + source_x];
            const uint16_t color =
                (font_byte & (1U << (source_y % 8U))) != 0U
                    ? foreground : background;
            for (uint8_t repeat_x = 0U; repeat_x < scale; repeat_x++) {
                const uint8_t pixel =
                    (uint8_t)((source_x * scale + repeat_x) * 2U);
                row_pixels[pixel] = (uint8_t)(color >> 8);
                row_pixels[pixel + 1U] = (uint8_t)color;
            }
        }
        for (uint8_t repeat_y = 0U; repeat_y < scale; repeat_y++) {
            if (!tft_transmit(row_pixels, (uint16_t)(width * 2U))) {
                tft_deselect();
                return;
            }
        }
    }
    tft_deselect();
}

void tft_st7789_draw_text(uint16_t x, uint16_t y, const char *string,
                          uint16_t foreground, uint16_t background,
                          uint8_t scale) {
    if (string == NULL || (scale != 1U && scale != 2U)) {
        return;
    }
    const uint16_t advance = (uint16_t)(TFT_FONT_WIDTH * scale);
    while (*string != '\0' && (uint32_t)x + advance <= TFT_WIDTH) {
        tft_st7789_draw_char(x, y, *string, foreground, background, scale);
        x = (uint16_t)(x + advance);
        string++;
    }
}

static void tft_st7789_draw_chinese(uint16_t x, uint16_t y,
                                    const uint8_t *glyph,
                                    uint16_t foreground,
                                    uint16_t background, uint8_t scale) {
    const uint16_t width = (uint16_t)(TFT_CHINESE_GLYPH_WIDTH * scale);
    const uint16_t height = (uint16_t)(TFT_CHINESE_GLYPH_HEIGHT * scale);
    if (glyph == NULL || (uint32_t)x + width > TFT_WIDTH ||
        (uint32_t)y + height > TFT_HEIGHT ||
        !tft_set_window(x, y, (uint16_t)(x + width - 1U),
                        (uint16_t)(y + height - 1U))) {
        return;
    }

    uint8_t row_pixels[TFT_CHINESE_GLYPH_WIDTH * 2U * 2U];
    HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_SET);
    tft_select();
    for (uint8_t source_y = 0U; source_y < TFT_CHINESE_GLYPH_HEIGHT;
         source_y++) {
        const uint16_t bits = (uint16_t)(((uint16_t)glyph[source_y * 2U]
                                          << 8U) |
                                         glyph[source_y * 2U + 1U]);
        for (uint8_t source_x = 0U; source_x < TFT_CHINESE_GLYPH_WIDTH;
             source_x++) {
            const uint16_t color =
                (bits & (uint16_t)(1U << (15U - source_x))) != 0U
                    ? foreground : background;
            for (uint8_t repeat_x = 0U; repeat_x < scale; repeat_x++) {
                const uint8_t pixel =
                    (uint8_t)((source_x * scale + repeat_x) * 2U);
                row_pixels[pixel] = (uint8_t)(color >> 8U);
                row_pixels[pixel + 1U] = (uint8_t)color;
            }
        }
        for (uint8_t repeat_y = 0U; repeat_y < scale; repeat_y++) {
            if (!tft_transmit(row_pixels, (uint16_t)(width * 2U))) {
                tft_deselect();
                return;
            }
        }
    }
    tft_deselect();
}

void tft_st7789_draw_utf8(uint16_t x, uint16_t y, const char *string,
                          uint16_t foreground, uint16_t background,
                          uint8_t scale) {
    if (string == NULL || (scale != 1U && scale != 2U)) {
        return;
    }
    while (*string != '\0') {
        const uint8_t first = (uint8_t)*string;
        if (first < 0x80U) {
            const uint16_t advance = (uint16_t)(TFT_FONT_WIDTH * scale);
            if ((uint32_t)x + advance > TFT_WIDTH) {
                break;
            }
            tft_st7789_draw_char(x, y, *string, foreground, background,
                                 scale);
            x = (uint16_t)(x + advance);
            string++;
            continue;
        }
        if ((first & 0xF0U) == 0xE0U && string[1] != '\0' &&
            string[2] != '\0') {
            const uint16_t codepoint = (uint16_t)(
                ((uint16_t)(first & 0x0FU) << 12U) |
                ((uint16_t)((uint8_t)string[1] & 0x3FU) << 6U) |
                ((uint8_t)string[2] & 0x3FU));
            const uint16_t advance =
                (uint16_t)(TFT_CHINESE_GLYPH_WIDTH * scale);
            if ((uint32_t)x + advance > TFT_WIDTH) {
                break;
            }
            const uint8_t *glyph = tft_chinese_font_find(codepoint);
            if (glyph != NULL) {
                tft_st7789_draw_chinese(x, y, glyph, foreground, background,
                                        scale);
            } else {
                tft_st7789_draw_char(x, y, '?', foreground, background,
                                     scale);
            }
            x = (uint16_t)(x + advance);
            string += 3;
            continue;
        }
        string++;
    }
}
