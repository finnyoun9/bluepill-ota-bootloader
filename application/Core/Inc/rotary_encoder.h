#ifndef ROTARY_ENCODER_H
#define ROTARY_ENCODER_H

#include <stdbool.h>
#include <stdint.h>

bool rotary_encoder_init(void);
int16_t rotary_encoder_get_delta(void);
bool rotary_encoder_button_pressed(void);

#endif
