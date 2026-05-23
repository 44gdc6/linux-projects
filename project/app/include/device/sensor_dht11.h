#ifndef __SENSOR_DHT11_H__
#define __SENSOR_DHT11_H__

#include "core/linkqueue.h"

int dht11_sensor_init(void **ctx);
int dht11_sensor_read(void *ctx, data_t *sample);
void dht11_sensor_deinit(void *ctx);

#endif
