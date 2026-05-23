#include <assert.h>
#include <string.h>

#include "device/weather_client.h"

static const char *sample_response =
    "HTTP/1.1 200 OK\r\n"
    "Transfer-Encoding: chunked\r\n"
    "\r\n"
    "12F\r\n"
    "{\"success\":\"1\",\"result\":[{\"days\":\"2026-05-04\",\"week\":\"星期日\",\"citynm\":\"西安\",\"weather\":\"晴\",\"temperature\":\"18℃/29℃\"},"
    "{\"days\":\"2026-05-05\",\"week\":\"星期一\",\"citynm\":\"西安\",\"weather\":\"多云\",\"temperature\":\"19℃/27℃\"}]}\r\n"
    "0\r\n\r\n";

static void test_parse_chunked_weather_response(void)
{
    weather_forecast_t forecast;

    memset(&forecast, 0, sizeof(forecast));
    assert(weather_parse_http_response(sample_response, &forecast) == 0);
    assert(forecast.valid == 1);
    assert(forecast.count == 2);
    assert(strcmp(forecast.days[0].day, "2026-05-04") == 0);
    assert(strcmp(forecast.days[0].week, "星期日") == 0);
    assert(strcmp(forecast.days[0].city, "西安") == 0);
    assert(strcmp(forecast.days[0].weather, "晴") == 0);
    assert(strcmp(forecast.days[0].temperature, "18℃/29℃") == 0);
    assert(strcmp(forecast.days[1].day, "2026-05-05") == 0);
}

static void test_today_snapshot_copies_first_day(void)
{
    weather_forecast_t forecast;
    weather_snapshot_t snapshot;

    memset(&forecast, 0, sizeof(forecast));
    memset(&snapshot, 0, sizeof(snapshot));

    strcpy(forecast.days[0].day, "2026-05-04");
    strcpy(forecast.days[0].week, "星期日");
    strcpy(forecast.days[0].city, "西安");
    strcpy(forecast.days[0].weather, "晴");
    strcpy(forecast.days[0].temperature, "18℃/29℃");
    forecast.count = 1;
    forecast.valid = 1;

    weather_snapshot_from_forecast(&forecast, &snapshot);
    assert(snapshot.valid == 1);
    assert(strcmp(snapshot.day, "2026-05-04") == 0);
    assert(strcmp(snapshot.week, "星期日") == 0);
    assert(strcmp(snapshot.city, "西安") == 0);
    assert(strcmp(snapshot.weather, "晴") == 0);
    assert(strcmp(snapshot.temperature, "18℃/29℃") == 0);
}

int main(void)
{
    test_parse_chunked_weather_response();
    test_today_snapshot_copies_first_day();
    return 0;
}
