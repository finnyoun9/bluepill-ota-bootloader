#ifndef BACK_BUTTON_H
#define BACK_BUTTON_H

#include <stdbool.h>

/* PA4 active-low button: connect PA4 -> momentary switch -> GND. */
bool back_button_init(void);
bool back_button_pressed(void);

#endif
