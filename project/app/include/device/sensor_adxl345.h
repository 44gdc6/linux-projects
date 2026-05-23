#ifndef __SENSOR_ADXL345_H__
#define __SENSOR_ADXL345_H__

#include "core/linkqueue.h"

int adxl345_sensor_init(void **ctx);
int adxl345_sensor_read(void *ctx, data_t *sample);
void adxl345_sensor_deinit(void *ctx);

#endif
