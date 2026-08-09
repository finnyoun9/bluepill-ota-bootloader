#ifndef BH1750_H
#define BH1750_H

#include <stdbool.h>
#include <stdint.h>

bool bh1750_init(void);
bool bh1750_read_lux(uint16_t *lux);

#endif
