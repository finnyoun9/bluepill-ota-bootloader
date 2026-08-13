#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_PAGE_MENU = 0,
    UI_PAGE_ENVIRONMENT,
    UI_PAGE_LIGHT,
    UI_PAGE_MOTION,
    UI_PAGE_SYSTEM,
    UI_PAGE_ABOUT
} UiPage_t;

#define UI_MENU_ITEM_COUNT  5U

typedef struct {
    int16_t temperature_centi_c;
    uint16_t humidity_centi_percent;
    uint32_t pressure_pa;
    uint16_t light_lux;
    uint32_t firmware_version;
    uint8_t led_percent;
    uint8_t control_selected;
    bool environment_valid;
    bool light_valid;
    bool pir_ready;
    bool pir_warmed_up;
    bool motion_detected;
    bool light_power_on;
    bool relay1_on;
    bool buzzer_on;
    bool light_auto_mode;
    bool control_editing;
    bool ui_chinese;
} UiStatus_t;

/* OLED is an always-on status display; TFT is the independent menu UI. */
bool ui_display_init(bool oled_bus_ready);
void ui_display_render(UiPage_t page, uint8_t selected,
                       const UiStatus_t *status, bool force);

#endif
