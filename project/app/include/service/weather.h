#ifndef __WEATHER_THREAD_H__
#define __WEATHER_THREAD_H__

#include "device/weather_client.h"

void weather_cache_reset(void);
int weather_cache_update(const weather_forecast_t *forecast);
int weather_cache_get(weather_forecast_t *forecast);
int weather_cache_get_snapshot(weather_snapshot_t *snapshot);
unsigned int weather_cache_get_version(void);
void *weather_thread(void *arg);

#endif
