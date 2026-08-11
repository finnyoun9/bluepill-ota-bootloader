#ifndef BUZZER_H
#define BUZZER_H

#include <stdbool.h>

bool buzzer_init(void);
void buzzer_set(bool on);
bool buzzer_get_state(void);

#endif /* BUZZER_H */
