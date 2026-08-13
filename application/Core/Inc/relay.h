/**
 * @file    relay.h
 * @brief   Two-channel active-low relay driver (PA2/PA3).
 */

#ifndef RELAY_H
#define RELAY_H

#include <stdbool.h>
#include <stdint.h>

#define RELAY_CHANNELS  2U
#define RELAY_UNUSED_CHANNEL  0U  /* Relay 1 / PA2 / no load connected */
#define RELAY_LIGHT_CHANNEL   1U  /* Relay 2 / PA3 / NO2 / WS2812B VCC */

bool relay_init(void);
void relay_set(uint8_t channel, bool on);
bool relay_get_state(uint8_t channel);
bool relay_auto_enabled(void);
void relay_set_auto(bool enabled);

#endif /* RELAY_H */
