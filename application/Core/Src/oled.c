#include "oled.h"

#include "env_i2c.h"
#include "oled_font.h"
#include "stm32f1xx_hal.h"

#define OLED_ADDRESS       0x3CU
#define OLED_WIDTH         128U
#define OLED_PAGE_COUNT    8U
#define OLED_CONTROL_CMD   0x00U
#define OLED_CONTROL_DATA  0x40U

/* The display API and SSD1306 sequence follow the Jiangke University
 * tutorial. The original software I2C transport is replaced by I2C1. */
static uint8_t g_oled_tx[OLED_WIDTH + 1U];

static bool oled_write(uint8_t control, const uint8_t *data, uint16_t length) {
    if (data == NULL || length == 0U || length > OLED_WIDTH) {
        return false;
    }

    g_oled_tx[0] = control;
    for (uint16_t i = 0U; i < length; i++) {
        g_oled_tx[i + 1U] = data[i];
    }
    return env_i2c_write(OLED_ADDRESS, g_oled_tx, (uint16_t)(length + 1U));
}

static bool oled_write_command(uint8_t command) {
    return oled_write(OLED_CONTROL_CMD, &command, 1U);
}

static bool oled_write_data(const uint8_t *data, uint16_t length) {
    return oled_write(OLED_CONTROL_DATA, data, length);
}

static void oled_set_cursor(uint8_t page, uint8_t x) {
    (void)oled_write_command((uint8_t)(0xB0U | page));
    (void)oled_write_command((uint8_t)(0x10U | ((x & 0xF0U) >> 4)));
    (void)oled_write_command((uint8_t)(x & 0x0FU));
}

static uint32_t oled_pow(uint32_t base, uint8_t exponent) {
    uint32_t result = 1U;
    while (exponent-- > 0U) {
        result *= base;
    }
    return result;
}

bool oled_init(void) {
    static const uint8_t init_commands[] = {
        0xAEU,
        0xD5U, 0x80U,
        0xA8U, 0x3FU,
        0xD3U, 0x00U,
        0x40U,
        0xA1U,
        0xC8U,
        0xDAU, 0x12U,
        0x81U, 0xCFU,
        0xD9U, 0xF1U,
        0xDBU, 0x30U,
        0xA4U,
        0xA6U,
        0x8DU, 0x14U,
        0xAFU
    };

    HAL_Delay(100U);
    if (!env_i2c_device_ready(OLED_ADDRESS)) {
        return false;
    }

    for (uint16_t i = 0U; i < sizeof(init_commands); i++) {
        if (!oled_write_command(init_commands[i])) {
            return false;
        }
    }

    oled_clear();
    return true;
}

void oled_clear(void) {
    uint8_t blank[OLED_WIDTH] = {0};

    for (uint8_t page = 0U; page < OLED_PAGE_COUNT; page++) {
        oled_set_cursor(page, 0U);
        (void)oled_write_data(blank, sizeof(blank));
    }
}

void oled_show_char(uint8_t line, uint8_t column, char character) {
    if (line < 1U || line > 4U || column < 1U || column > 16U) {
        return;
    }
    if (character < ' ' || character > '~') {
        character = '?';
    }

    const uint8_t *glyph =
        &OLED_F8x16[((uint8_t)character - (uint8_t)' ') * 16U];
    const uint8_t x = (uint8_t)((column - 1U) * 8U);
    const uint8_t top_page = (uint8_t)((line - 1U) * 2U);

    oled_set_cursor(top_page, x);
    (void)oled_write_data(glyph, 8U);
    oled_set_cursor((uint8_t)(top_page + 1U), x);
    (void)oled_write_data(&glyph[8], 8U);
}

void oled_show_string(uint8_t line, uint8_t column, const char *string) {
    if (string == NULL) {
        return;
    }

    while (*string != '\0' && column <= 16U) {
        oled_show_char(line, column, *string);
        string++;
        column++;
    }
}

void oled_show_num(uint8_t line, uint8_t column, uint32_t number,
                   uint8_t length) {
    for (uint8_t i = 0U; i < length; i++) {
        const uint32_t divisor = oled_pow(10U, (uint8_t)(length - i - 1U));
        oled_show_char(line, (uint8_t)(column + i),
                       (char)('0' + (number / divisor) % 10U));
    }
}

void oled_show_signed_num(uint8_t line, uint8_t column, int32_t number,
                          uint8_t length) {
    uint32_t magnitude;

    if (number < 0) {
        oled_show_char(line, column, '-');
        magnitude = (uint32_t)(-(number + 1)) + 1U;
    } else {
        oled_show_char(line, column, '+');
        magnitude = (uint32_t)number;
    }

    oled_show_num(line, (uint8_t)(column + 1U), magnitude, length);
}
