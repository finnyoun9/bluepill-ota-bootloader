/**
 * @file    relay.h
 * @brief   Two-channel relay driver (PB12/PB13).
 */

#ifndef RELAY_H
#define RELAY_H

#include <stdbool.h>
#include <stdint.h>

#define RELAY_CHANNELS  2U

bool relay_init(void);
void relay_set(uint8_t channel, bool on);
bool relay_get_state(uint8_t channel);
bool relay_auto_enabled(void);
void relay_set_auto(bool enabled);

#endif /* RELAY_H */
