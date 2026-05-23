#ifndef __SENSOR_MQ3_H__
#define __SENSOR_MQ3_H__

#include "core/linkqueue.h"

int mq3_sensor_init(void **ctx);
int mq3_sensor_read(void *ctx, data_t *sample);
void mq3_sensor_deinit(void *ctx);

#endif
