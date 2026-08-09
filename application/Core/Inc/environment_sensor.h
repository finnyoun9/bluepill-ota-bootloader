#ifndef ENVIRONMENT_SENSOR_H
#define ENVIRONMENT_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t temperature_centi_c;
    uint16_t humidity_centi_percent;
    uint32_t pressure_pa;
} EnvironmentReading_t;

bool environment_sensor_init(void);
bool environment_sensor_start_measurement(void);
bool environment_sensor_read(EnvironmentReading_t *reading);

#endif
