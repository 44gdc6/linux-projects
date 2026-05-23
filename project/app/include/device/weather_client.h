#ifndef __WEATHER_CLIENT_H__
#define __WEATHER_CLIENT_H__

#include <time.h>

#define WEATHER_FIELD_LEN 64
#define WEATHER_FORECAST_MAX_DAYS 7

typedef struct weather_day {
    char day[WEATHER_FIELD_LEN];
    char week[WEATHER_FIELD_LEN];
    char city[WEATHER_FIELD_LEN];
    char weather[WEATHER_FIELD_LEN];
    char temperature[WEATHER_FIELD_LEN];
} weather_day_t;

typedef struct weather_forecast {
    weather_day_t days[WEATHER_FORECAST_MAX_DAYS];
    int count;
    int valid;
    time_t updated_at;
} weather_forecast_t;

typedef struct weather_snapshot {
    int valid;
    char day[WEATHER_FIELD_LEN];
    char week[WEATHER_FIELD_LEN];
    char city[WEATHER_FIELD_LEN];
    char weather[WEATHER_FIELD_LEN];
    char temperature[WEATHER_FIELD_LEN];
} weather_snapshot_t;

int weather_client_fetch(const char *city, weather_forecast_t *forecast);
int weather_parse_http_response(const char *response, weather_forecast_t *forecast);
void weather_snapshot_from_forecast(const weather_forecast_t *forecast, weather_snapshot_t *snapshot);

#endif
