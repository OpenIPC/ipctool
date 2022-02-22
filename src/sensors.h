#ifndef SENSORID_H
#define SENSORID_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char sensor_id[128];
    char control[4];
    char vendor[32];
    uint16_t addr;
    unsigned int reg_width;
    unsigned int data_width;
    cJSON *j_sensor;
    cJSON *j_params;
} sensor_ctx_t;

const char *getsensoridentity();
cJSON *detect_sensors();
bool getsensorid(sensor_ctx_t *ctx);

#endif /* SENSORID_H */
