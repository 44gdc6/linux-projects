#include "service/weather.h"

#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "config/app_config.h"
#include "core/log.h"
#include "device/weather_client.h"

typedef struct weather_cache {
    pthread_mutex_t lock;
    weather_forecast_t forecast;
    unsigned int version;
} weather_cache_t;

static weather_cache_t g_weather_cache = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .forecast = { { { {0} } }, 0, 0, 0 },
    .version = 0,
};

void weather_cache_reset(void)
{
    pthread_mutex_lock(&g_weather_cache.lock);
    memset(&g_weather_cache.forecast, 0, sizeof(g_weather_cache.forecast));
    g_weather_cache.version = 0;
    pthread_mutex_unlock(&g_weather_cache.lock);
}

int weather_cache_update(const weather_forecast_t *forecast)
{
    if (forecast == NULL) {
        return -1;
    }

    pthread_mutex_lock(&g_weather_cache.lock);
    memcpy(&g_weather_cache.forecast, forecast, sizeof(g_weather_cache.forecast));
    ++g_weather_cache.version;
    pthread_mutex_unlock(&g_weather_cache.lock);
    return 0;
}

int weather_cache_get(weather_forecast_t *forecast)
{
    if (forecast == NULL) {
        return -1;
    }

    pthread_mutex_lock(&g_weather_cache.lock);
    memcpy(forecast, &g_weather_cache.forecast, sizeof(*forecast));
    pthread_mutex_unlock(&g_weather_cache.lock);
    return forecast->valid ? 0 : -1;
}

int weather_cache_get_snapshot(weather_snapshot_t *snapshot)
{
    weather_forecast_t forecast;

    if (snapshot == NULL) {
        return -1;
    }

    pthread_mutex_lock(&g_weather_cache.lock);
    memcpy(&forecast, &g_weather_cache.forecast, sizeof(forecast));
    pthread_mutex_unlock(&g_weather_cache.lock);

    weather_snapshot_from_forecast(&forecast, snapshot);
    return snapshot->valid ? 0 : -1;
}

unsigned int weather_cache_get_version(void)
{
    unsigned int version = 0;

    pthread_mutex_lock(&g_weather_cache.lock);
    version = g_weather_cache.version;
    pthread_mutex_unlock(&g_weather_cache.lock);
    return version;
}

void *weather_thread(void *arg)
{
    weather_forecast_t forecast;

    (void)arg;
    weather_cache_reset();

    while (1) {
        memset(&forecast, 0, sizeof(forecast));
        if (weather_client_fetch(WEATHER_CITY, &forecast) == 0) {
            weather_cache_update(&forecast);
            LOG_INFO("weather updated city=%s days=%d", WEATHER_CITY, forecast.count);
        } else {
            LOG_ERROR("weather fetch failed for city=%s", WEATHER_CITY);
        }

        sleep(WEATHER_REFRESH_INTERVAL_SEC);
    }

    return NULL;
}
