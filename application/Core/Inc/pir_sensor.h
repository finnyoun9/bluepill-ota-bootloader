#ifndef PIR_SENSOR_H
#define PIR_SENSOR_H

#include <stdbool.h>

bool pir_sensor_init(void);
bool pir_sensor_motion_detected(void);

#endif
