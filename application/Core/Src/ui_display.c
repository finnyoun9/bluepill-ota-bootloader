#include "ui_display.h"

#include <string.h>

#include "oled.h"
#include "tft_st7789.h"

#define TFT_BG          0x0008U
#define TFT_SURFACE     0x10C3U
#define TFT_SELECTED    0x0452U
#define TFT_BORDER      0x2966U
#define TFT_WHITE       0xFFFFU
#define TFT_MUTED       0x8410U
#define TFT_CYAN        0x4F9FU
#define TFT_TEAL        0x37F6U
#define TFT_GREEN       0x4F57U
#define TFT_YELLOW      0xFEC0U
#define TFT_RED         0xF2AAU

static bool g_oled_ready;
static bool g_tft_ready;
static bool g_tft_cache_valid;
static UiPage_t g_cached_page;
static uint8_t g_cached_selected;
static UiStatus_t g_cached_status;
static char g_oled_lines[4][17];
static char g_oled_lines_zh[4][24];

static const char * const g_menu_labels_en[UI_MENU_ITEM_COUNT] = {
    "ENVIRONMENT", "LIGHT CONTROL", "MOTION", "SYSTEM", "ABOUT"
};
static const char * const g_menu_labels_zh[UI_MENU_ITEM_COUNT] = {
    "环境", "灯光", "人体", "系统", "关于"
};

static void line_reset(char *line) {
    memset(line, ' ', 16U);
    line[16] = '\0';
}

static void line_put_text(char *line, uint8_t position, const char *text) {
    while (*text != '\0' && position < 16U) {
        line[position++] = *text++;
    }
}

static void line_put_u32(char *line, uint8_t position, uint8_t width,
                         uint32_t value, bool zero_pad) {
    for (uint8_t i = 0U; i < width; i++) {
        const uint8_t index = (uint8_t)(position + width - i - 1U);
        if (index >= 16U) {
            continue;
        }
        line[index] = (char)('0' + value % 10U);
        value /= 10U;
        if (value == 0U && !zero_pad) {
            while (++i < width) {
                const uint8_t blank = (uint8_t)(position + width - i - 1U);
                if (blank < 16U) {
                    line[blank] = ' ';
                }
            }
            break;
        }
    }
}

static uint8_t clamped_percent(uint8_t value) {
    return value > 100U ? 100U : value;
}

static void zh_put_decimal(char *buf, uint8_t *len, uint32_t value) {
    char tmp[8];
    uint8_t n = 0U;

    do {
        tmp[n++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);

    while (n > 0U) {
        buf[(*len)++] = tmp[--n];
    }
}

static void zh_put_fixed(char *buf, uint8_t *len, uint32_t value,
                         uint8_t width) {
    char tmp[8];
    for (uint8_t i = 0U; i < width; i++) {
        tmp[width - 1U - i] = (char)('0' + value % 10U);
        value /= 10U;
    }
    for (uint8_t i = 0U; i < width; i++) {
        buf[(*len)++] = tmp[i];
    }
}

static void oled_render_status_chinese(const UiStatus_t *status, bool force) {
    char lines[4][24];
    uint8_t len[4];

    for (uint8_t i = 0U; i < 4U; i++) {
        lines[i][0] = '\0';
        len[i] = 0U;
    }

    strcpy(lines[0], "温度");
    len[0] = 6U;
    if (status->environment_valid) {
        const int32_t temperature = status->temperature_centi_c;
        const uint32_t magnitude =
            (uint32_t)(temperature < 0 ? -temperature : temperature);
        lines[0][len[0]++] = (char)(temperature < 0 ? '-' : '+');
        zh_put_fixed(lines[0], &len[0], magnitude / 100U, 2U);
        lines[0][len[0]++] = '.';
        zh_put_fixed(lines[0], &len[0], (magnitude % 100U) / 10U, 1U);
        lines[0][len[0]++] = 'C';
    } else {
        strcpy(lines[0] + len[0], "--.-C");
        len[0] += 5U;
    }

    strcpy(lines[1], "湿度");
    len[1] = 6U;
    if (status->environment_valid) {
        zh_put_decimal(lines[1], &len[1],
                       status->humidity_centi_percent / 100U);
    } else {
        strcpy(lines[1] + len[1], "---");
        len[1] += 3U;
    }
    lines[1][len[1]++] = '%';
    lines[1][len[1]++] = ' ';
    strcpy(lines[1] + len[1], "人体");
    len[1] += 6U;
    {
        const char *state = !status->pir_ready
                                ? "ERR"
                                : (!status->pir_warmed_up
                                       ? "WUP"
                                       : (status->motion_detected ? "ON"
                                                                  : "OFF"));
        strcpy(lines[1] + len[1], state);
        len[1] += (uint8_t)strlen(state);
    }

    strcpy(lines[2], "光照");
    len[2] = 6U;
    if (status->light_valid) {
        uint32_t lux = status->light_lux;
        if (lux > 9999U) {
            lux = 9999U;
        }
        zh_put_decimal(lines[2], &len[2], lux);
    } else {
        strcpy(lines[2] + len[2], "----");
        len[2] += 4U;
    }
    lines[2][len[2]++] = ' ';
    strcpy(lines[2] + len[2], "灯光");
    len[2] += 6U;
    strcpy(lines[2] + len[2], status->light_power_on ? "ON" : "OFF");
    len[2] += (uint8_t)(status->light_power_on ? 2U : 3U);

    strcpy(lines[3], "气压");
    len[3] = 6U;
    if (status->environment_valid) {
        const uint32_t pressure_deci_hpa = (status->pressure_pa + 5U) / 10U;
        zh_put_decimal(lines[3], &len[3], pressure_deci_hpa / 10U);
    } else {
        strcpy(lines[3] + len[3], "----");
        len[3] += 4U;
    }
    lines[3][len[3]++] = ' ';
    strcpy(lines[3] + len[3], "固件");
    len[3] += 6U;
    zh_put_fixed(lines[3], &len[3], status->firmware_version, 3U);

    for (uint8_t line = 0U; line < 4U; line++) {
        lines[line][len[line]] = '\0';
        if (force || strcmp(lines[line], g_oled_lines_zh[line]) != 0) {
            oled_show_utf8((uint8_t)(line + 1U), 1U, lines[line]);
            strcpy(g_oled_lines_zh[line], lines[line]);
        }
    }
}

static void oled_render_status(const UiStatus_t *status, bool force) {
    if (!g_oled_ready) {
        return;
    }
    if (status->ui_chinese) {
        oled_render_status_chinese(status, force);
        return;
    }

    char lines[4][17];
    for (uint8_t i = 0U; i < 4U; i++) {
        line_reset(lines[i]);
    }

    line_put_text(lines[0], 0U, "T:");
    if (status->environment_valid) {
        const int32_t temperature = status->temperature_centi_c;
        const uint32_t magnitude =
            (uint32_t)(temperature < 0 ? -temperature : temperature);
        lines[0][2] = temperature < 0 ? '-' : '+';
        line_put_u32(lines[0], 3U, 2U, magnitude / 100U, true);
        lines[0][5] = '.';
        line_put_u32(lines[0], 6U, 1U, (magnitude % 100U) / 10U, true);
        lines[0][7] = 'C';
        line_put_text(lines[0], 9U, "H:");
        line_put_u32(lines[0], 11U, 3U,
                     status->humidity_centi_percent / 100U, false);
        lines[0][14] = '%';
    } else {
        line_put_text(lines[0], 2U, "--.-C H:---%");
    }

    line_put_text(lines[1], 0U, "L:");
    if (status->light_valid) {
        line_put_u32(lines[1], 2U, 4U, status->light_lux, false);
    } else {
        line_put_text(lines[1], 2U, " ERR");
    }
    line_put_text(lines[1], 7U, "LED:");
    line_put_u32(lines[1], 11U, 3U, clamped_percent(status->led_percent),
                 false);
    lines[1][14] = '%';

    line_put_text(lines[2], 0U, "P:");
    if (status->environment_valid) {
        const uint32_t pressure_deci_hpa = (status->pressure_pa + 5U) / 10U;
        line_put_u32(lines[2], 2U, 4U, pressure_deci_hpa / 10U, false);
        lines[2][6] = '.';
        line_put_u32(lines[2], 7U, 1U, pressure_deci_hpa % 10U, true);
    } else {
        line_put_text(lines[2], 2U, " ----.-");
    }
    line_put_text(lines[2], 9U, "PIR:");
    line_put_text(lines[2], 13U,
                  !status->pir_ready ? "ERR" :
                  (!status->pir_warmed_up ? "WUP" :
                   (status->motion_detected ? "ON" : "OFF")));

    line_put_text(lines[3], 0U,
                  status->light_power_on ? "LIGHT:ON " : "LIGHT:OFF");
    line_put_text(lines[3], 10U, "FW:");
    line_put_u32(lines[3], 13U, 3U, status->firmware_version, true);

    for (uint8_t line = 0U; line < 4U; line++) {
        if (force || memcmp(lines[line], g_oled_lines[line], 16U) != 0) {
            oled_show_string((uint8_t)(line + 1U), 1U, lines[line]);
            memcpy(g_oled_lines[line], lines[line], 17U);
        }
    }
}

static void tft_draw_header(const char *title, const char *subtitle) {
    tft_st7789_fill_rect(0U, 0U, 240U, 70U, TFT_BG);
    tft_st7789_fill_rect(14U, 16U, 4U, 38U, TFT_TEAL);
    tft_st7789_draw_utf8(26U, 14U, title, TFT_WHITE, TFT_BG, 2U);
    tft_st7789_draw_utf8(27U, 48U, subtitle, TFT_MUTED, TFT_BG, 1U);
    tft_st7789_fill_rect(14U, 68U, 212U, 1U, TFT_BORDER);
}

static void tft_draw_footer(const char *hint) {
    tft_st7789_fill_rect(0U, 292U, 240U, 28U, TFT_BG);
    tft_st7789_fill_rect(14U, 292U, 212U, 1U, TFT_BORDER);
    tft_st7789_draw_utf8(18U, 300U, hint, TFT_MUTED, TFT_BG, 1U);
}

static void tft_draw_menu_card(uint8_t item, bool selected, bool chinese) {
    const uint16_t y = (uint16_t)(79U + item * 42U);
    const uint16_t background = selected ? TFT_SELECTED : TFT_SURFACE;
    const uint16_t text = selected ? TFT_WHITE : TFT_MUTED;
    tft_st7789_fill_rect(14U, y, 212U, 34U, background);
    tft_st7789_fill_rect(14U, y, selected ? 5U : 1U, 34U,
                         selected ? TFT_CYAN : TFT_BORDER);
    tft_st7789_draw_utf8(29U, (uint16_t)(y + 9U),
                         chinese ? g_menu_labels_zh[item]
                                 : g_menu_labels_en[item],
                         text, background, 1U);
    if (selected) {
        tft_st7789_draw_text(205U, (uint16_t)(y + 9U), ">",
                              TFT_YELLOW, background, 1U);
    }
}

static void tft_draw_light_badge(const UiStatus_t *status) {
    const uint16_t color = status->light_power_on ? TFT_GREEN : TFT_MUTED;
    tft_st7789_fill_rect(150U, 20U, 76U, 24U, TFT_SURFACE);
    if (status->ui_chinese) {
        tft_st7789_draw_utf8(158U, 24U,
                             status->light_power_on ? "灯光开" : "灯光关",
                             color, TFT_SURFACE, 1U);
    } else {
        tft_st7789_draw_text(158U, 24U,
                             status->light_power_on ? "LIGHT ON" : "LIGHT OFF",
                             color, TFT_SURFACE, 1U);
    }
}

static void tft_draw_menu(uint8_t selected, const UiStatus_t *status) {
    tft_st7789_clear();
    tft_draw_header("ENVLINK", "LOCAL CONTROL");
    tft_draw_light_badge(status);
    for (uint8_t item = 0U; item < UI_MENU_ITEM_COUNT; item++) {
        tft_draw_menu_card(item, item == selected, status->ui_chinese);
    }
    tft_draw_footer(status->ui_chinese ? "旋转选择 按下确认"
                                       : "ROTATE SELECT  PRESS ENTER");
}

static void tft_draw_page_shell(UiPage_t page, const UiStatus_t *status) {
    tft_st7789_clear();
    switch (page) {
    case UI_PAGE_ENVIRONMENT:
        tft_draw_header(status->ui_chinese ? "环境" : "ENV",
                        "AHT20 + BMP280");
        break;
    case UI_PAGE_LIGHT:
        tft_draw_header(status->ui_chinese ? "灯光" : "LIGHT",
                        "BH1750 + WS2812B");
        break;
    case UI_PAGE_MOTION:
        tft_draw_header(status->ui_chinese ? "人体" : "MOTION",
                        "HC-SR501");
        break;
    case UI_PAGE_SYSTEM:
        tft_draw_header(status->ui_chinese ? "系统" : "SYSTEM",
                        status->ui_chinese ? "设备信息" : "DEVICE INFO");
        break;
    case UI_PAGE_ABOUT:
        tft_draw_header(status->ui_chinese ? "关于" : "ABOUT",
                        "ENVLINK CONTROLLER");
        tft_st7789_draw_utf8(22U, 94U, status->ui_chinese ? "作者" : "AUTHOR",
                             TFT_MUTED, TFT_BG, 1U);
        tft_st7789_draw_text(88U, 94U, "Finn", TFT_CYAN, TFT_BG, 2U);
        tft_st7789_draw_text(22U, 136U, "GitHub", TFT_MUTED, TFT_BG, 1U);
        tft_st7789_draw_text(88U, 136U, "finnyoun9", TFT_WHITE, TFT_BG, 1U);
        tft_st7789_draw_utf8(22U, 164U, status->ui_chinese ? "硬件" : "HARDWARE",
                             TFT_MUTED, TFT_BG, 1U);
        tft_st7789_draw_text(88U, 164U, "Blue Pill F103", TFT_WHITE, TFT_BG, 1U);
        tft_st7789_draw_utf8(22U, 192U, status->ui_chinese ? "系统" : "SYSTEM",
                             TFT_MUTED, TFT_BG, 1U);
        tft_st7789_draw_text(88U, 192U, "FreeRTOS", TFT_WHITE, TFT_BG, 1U);
        break;
    case UI_PAGE_MENU:
    default:
        break;
    }
    tft_draw_footer(status->ui_chinese
                        ? (page == UI_PAGE_LIGHT ? "旋转选择 按下确认"
                                                 : "按下切换 返回菜单")
                        : (page == UI_PAGE_LIGHT ? "ROTATE SELECT  PRESS ENTER"
                                                 : "PRESS TOGGLE  BACK MENU"));
}

static void tft_draw_value_row(uint16_t y, const char *label,
                               const char *value, uint16_t accent) {
    tft_st7789_fill_rect(16U, y, 208U, 48U, TFT_SURFACE);
    tft_st7789_fill_rect(16U, y, 4U, 48U, accent);
    tft_st7789_draw_utf8(30U, (uint16_t)(y + 8U), label, TFT_MUTED,
                         TFT_SURFACE, 1U);
    tft_st7789_draw_utf8(112U, (uint16_t)(y + 8U), value, TFT_WHITE,
                         TFT_SURFACE, 2U);
}

static void format_temperature(char *text, const UiStatus_t *status) {
    line_reset(text);
    if (!status->environment_valid) {
        line_put_text(text, 0U, "ERROR");
        text[5] = '\0';
        return;
    }
    const int32_t temperature = status->temperature_centi_c;
    const uint32_t magnitude =
        (uint32_t)(temperature < 0 ? -temperature : temperature);
    text[0] = temperature < 0 ? '-' : '+';
    line_put_u32(text, 1U, 2U, magnitude / 100U, true);
    text[3] = '.';
    line_put_u32(text, 4U, 1U, (magnitude % 100U) / 10U, true);
    text[5] = 'C';
    text[6] = '\0';
}

static void tft_update_environment(const UiStatus_t *status) {
    char value[17];
    format_temperature(value, status);
    tft_draw_value_row(82U, status->ui_chinese ? "温度" : "TEMP",
                       value, TFT_RED);

    line_reset(value);
    if (status->environment_valid) {
        line_put_u32(value, 0U, 3U,
                     status->humidity_centi_percent / 100U, false);
        line_put_text(value, 3U, "%");
        value[4] = '\0';
    } else {
        line_put_text(value, 0U, "ERROR");
        value[5] = '\0';
    }
    tft_draw_value_row(140U, status->ui_chinese ? "湿度" : "HUM",
                       value, TFT_CYAN);

    line_reset(value);
    if (status->environment_valid) {
        line_put_u32(value, 0U, 4U, status->pressure_pa / 100U, false);
        line_put_text(value, 4U, "hPa");
        value[7] = '\0';
    } else {
        line_put_text(value, 0U, "ERROR");
        value[5] = '\0';
    }
    tft_draw_value_row(198U, status->ui_chinese ? "气压" : "PRES",
                       value, TFT_YELLOW);
}

static void tft_draw_control_row(uint16_t y, const char *label,
                                 const char *value, uint16_t value_color,
                                 bool selected) {
    const uint16_t background = selected ? TFT_SELECTED : TFT_SURFACE;
    tft_st7789_fill_rect(16U, y, 208U, 48U, background);
    tft_st7789_fill_rect(16U, y, selected ? 5U : 1U, 48U,
                         selected ? TFT_CYAN : TFT_BORDER);
    tft_st7789_draw_utf8(30U, (uint16_t)(y + 16U), label,
                         selected ? TFT_WHITE : TFT_MUTED, background, 1U);
    tft_st7789_draw_utf8(142U, (uint16_t)(y + 16U), value, value_color,
                         background, 1U);
}

static void tft_update_light(const UiStatus_t *status) {
    char value[17];
    line_reset(value);
    if (status->light_valid) {
        line_put_u32(value, 0U, 4U, status->light_lux, false);
        line_put_text(value, 4U, " lux");
    } else {
        line_put_text(value, 0U, "ERROR");
    }
    tft_st7789_fill_rect(16U, 76U, 208U, 22U, TFT_BG);
    tft_st7789_draw_utf8(20U, 80U,
                         status->ui_chinese ? "环境光" : "AMBIENT",
                         TFT_MUTED, TFT_BG, 1U);
    tft_st7789_draw_text(132U, 80U, value, TFT_YELLOW, TFT_BG, 1U);

    tft_draw_control_row(104U,
                         status->ui_chinese ? "开关" : "POWER",
                         status->light_power_on ? "ON" : "OFF",
                         status->light_power_on ? TFT_GREEN : TFT_RED,
                         status->control_selected == 0U);
    tft_draw_control_row(158U,
                         status->ui_chinese ? "模式" : "MODE",
                         status->light_auto_mode ? "AUTO" : "MANUAL",
                         status->light_auto_mode ? TFT_CYAN : TFT_YELLOW,
                         status->control_selected == 1U);

    line_reset(value);
    line_put_u32(value, 0U, 3U, clamped_percent(status->led_percent), false);
    line_put_text(value, 3U, "%");
    value[4] = '\0';
    tft_draw_control_row(212U,
                         status->ui_chinese ? "亮度" : "BRIGHT",
                         value,
                         status->light_auto_mode ? TFT_MUTED : TFT_WHITE,
                         status->control_selected == 2U);
    tft_st7789_fill_rect(30U, 267U, 176U, 9U, TFT_BORDER);
    const uint16_t filled = (uint16_t)(
        (uint32_t)clamped_percent(status->led_percent) * 176U / 100U);
    if (filled > 0U) {
        tft_st7789_fill_rect(30U, 267U, filled, 9U,
                             status->control_editing ? TFT_YELLOW : TFT_TEAL);
    }
}

static void tft_update_motion(const UiStatus_t *status) {
    const char *state = !status->pir_ready
                            ? (status->ui_chinese ? "故障" : "ERROR")
                            : (!status->pir_warmed_up
                                   ? (status->ui_chinese ? "预热" : "WARMUP")
                                   : (status->motion_detected
                                          ? (status->ui_chinese ? "检测" :
                                                                  "DETECTED")
                                          : (status->ui_chinese ? "无人" :
                                                                  "CLEAR")));
    const uint16_t color = status->motion_detected ? TFT_YELLOW : TFT_GREEN;
    tft_st7789_fill_rect(16U, 92U, 208U, 138U, TFT_SURFACE);
    tft_st7789_draw_utf8(28U, 110U,
                         status->ui_chinese ? "状态" : "CURRENT STATE",
                         TFT_MUTED, TFT_SURFACE, 1U);
    tft_st7789_draw_utf8(28U, 146U, state, color, TFT_SURFACE, 2U);
    tft_st7789_draw_text(28U, 194U,
                          status->motion_detected ? "OUTPUT HIGH" :
                                                    "OUTPUT LOW",
                          TFT_WHITE, TFT_SURFACE, 1U);
}

static void tft_update_system(const UiStatus_t *status) {
    char value[17];
    line_reset(value);
    line_put_text(value, 0U, "v");
    line_put_u32(value, 1U, 3U, status->firmware_version, true);
    value[4] = '\0';
    tft_draw_value_row(82U, status->ui_chinese ? "固件版本" : "FIRMWARE",
                       value, TFT_CYAN);
    tft_draw_value_row(140U, status->ui_chinese ? "型号" : "MODEL",
                       "ENVLINK", TFT_GREEN);
    tft_draw_value_row(198U, status->ui_chinese ? "语言" : "LANGUAGE",
                       status->ui_chinese ? "中文" : "ENGLISH",
                       TFT_YELLOW);
}

static void tft_update_dynamic(UiPage_t page, const UiStatus_t *status) {
    switch (page) {
    case UI_PAGE_ENVIRONMENT:
        tft_update_environment(status);
        break;
    case UI_PAGE_LIGHT:
        tft_update_light(status);
        break;
    case UI_PAGE_MOTION:
        tft_update_motion(status);
        break;
    case UI_PAGE_SYSTEM:
        tft_update_system(status);
        break;
    case UI_PAGE_MENU:
        tft_draw_light_badge(status);
        break;
    case UI_PAGE_ABOUT:
    default:
        break;
    }
}

static bool tft_page_data_changed(UiPage_t page,
                                  const UiStatus_t *current,
                                  const UiStatus_t *previous) {
    switch (page) {
    case UI_PAGE_MENU:
        return current->light_power_on != previous->light_power_on ||
               current->ui_chinese != previous->ui_chinese;
    case UI_PAGE_ENVIRONMENT:
        return current->environment_valid != previous->environment_valid ||
               current->temperature_centi_c !=
                   previous->temperature_centi_c ||
               current->humidity_centi_percent !=
                   previous->humidity_centi_percent ||
               current->pressure_pa != previous->pressure_pa;
    case UI_PAGE_LIGHT:
        return current->light_valid != previous->light_valid ||
               current->light_lux != previous->light_lux ||
               current->light_power_on != previous->light_power_on ||
               current->led_percent != previous->led_percent ||
               current->light_auto_mode != previous->light_auto_mode ||
               current->control_selected != previous->control_selected ||
               current->control_editing != previous->control_editing ||
               current->ui_chinese != previous->ui_chinese;
    case UI_PAGE_MOTION:
        return current->pir_ready != previous->pir_ready ||
               current->pir_warmed_up != previous->pir_warmed_up ||
               current->motion_detected != previous->motion_detected;
    case UI_PAGE_SYSTEM:
        return current->firmware_version != previous->firmware_version ||
               current->light_power_on != previous->light_power_on ||
               current->relay1_on != previous->relay1_on ||
               current->buzzer_on != previous->buzzer_on ||
               current->ui_chinese != previous->ui_chinese;
    case UI_PAGE_ABOUT:
    default:
        return false;
    }
}

bool ui_display_init(bool oled_bus_ready) {
    memset(g_oled_lines, 0, sizeof(g_oled_lines));
    memset(g_oled_lines_zh, 0, sizeof(g_oled_lines_zh));
    g_oled_ready = oled_bus_ready && oled_init();
    g_tft_ready = tft_st7789_init();
    g_tft_cache_valid = false;
    return g_oled_ready || g_tft_ready;
}

void ui_display_render(UiPage_t page, uint8_t selected,
                       const UiStatus_t *status, bool force) {
    if (status == NULL) {
        return;
    }

    oled_render_status(status, force);
    if (!g_tft_ready) {
        return;
    }

    const bool page_changed = !g_tft_cache_valid || page != g_cached_page;
    const bool language_changed = g_tft_cache_valid &&
        status->ui_chinese != g_cached_status.ui_chinese;
    const bool status_changed = !g_tft_cache_valid ||
        tft_page_data_changed(page, status, &g_cached_status);

    if (force || page_changed || language_changed) {
        if (page == UI_PAGE_MENU) {
            tft_draw_menu(selected, status);
        } else {
            tft_draw_page_shell(page, status);
            tft_update_dynamic(page, status);
        }
    } else if (page == UI_PAGE_MENU && selected != g_cached_selected) {
        tft_draw_menu_card(g_cached_selected, false, status->ui_chinese);
        tft_draw_menu_card(selected, true, status->ui_chinese);
        if (status_changed) {
            tft_draw_light_badge(status);
        }
    } else if (status_changed) {
        tft_update_dynamic(page, status);
    }

    g_cached_page = page;
    g_cached_selected = selected;
    g_cached_status = *status;
    g_tft_cache_valid = true;
}
