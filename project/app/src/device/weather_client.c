#include "device/weather_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "device/cJSON.h"

#define WEATHER_API_IP "103.205.5.206"
#define WEATHER_API_HOST "api.k780.com"
#define WEATHER_API_PORT 80
#define WEATHER_REQ_BUF_SIZE 4096
#define WEATHER_RESP_BUF_SIZE 16384

static void weather_copy_field(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src);
}

static int weather_create_tcp_connection(const char *ip, int port)
{
#ifdef _WIN32
    (void)ip;
    (void)port;
    return -1;
#else
    int ret = 0;
    int sockfd = 0;
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("weather socket failed");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((unsigned short)port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    ret = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret == -1) {
        perror("weather connect failed");
        close(sockfd);
        return -1;
    }

    return sockfd;
#endif
}

static int weather_send_http_request(int sockfd, const char *city)
{
#ifdef _WIN32
    (void)sockfd;
    (void)city;
    return -1;
#else
    char path[1024];
    char request[WEATHER_REQ_BUF_SIZE];
    ssize_t sent = 0;

    snprintf(path,
             sizeof(path),
             "/?app=weather.future&weaid=%s&appkey=10003&sign=b59bc3ef6191eb9f747dd4e83c99f2a4&format=json",
             city);
    snprintf(request,
             sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: project-weather-client/1.0\r\n"
             "Accept: application/json\r\n"
             "Connection: close\r\n"
             "\r\n",
             path,
             WEATHER_API_HOST);

    sent = send(sockfd, request, strlen(request), 0);
    if (sent == -1) {
        perror("weather send failed");
        return -1;
    }

    return 0;
#endif
}

static int weather_recv_http_response(int sockfd, char *buffer, size_t size)
{
#ifdef _WIN32
    (void)sockfd;
    (void)buffer;
    (void)size;
    return -1;
#else
    ssize_t received = 0;
    size_t total = 0;

    while (total + 1 < size) {
        received = recv(sockfd, buffer + total, size - total - 1, 0);
        if (received < 0) {
            perror("weather recv failed");
            return -1;
        }
        if (received == 0) {
            break;
        }
        total += (size_t)received;
    }

    buffer[total] = '\0';
    return total > 0 ? 0 : -1;
#endif
}

static const char *weather_skip_chunk_prefix(const char *body)
{
    const char *line_end = NULL;

    line_end = strstr(body, "\r\n");
    if (line_end == NULL) {
        return NULL;
    }

    return line_end + 2;
}

static int weather_parse_result_array(cJSON *result, weather_forecast_t *forecast)
{
    int count = 0;
    int index = 0;

    if (!cJSON_IsArray(result)) {
        return -1;
    }

    count = cJSON_GetArraySize(result);
    if (count > WEATHER_FORECAST_MAX_DAYS) {
        count = WEATHER_FORECAST_MAX_DAYS;
    }

    for (index = 0; index < count; ++index) {
        cJSON *item = cJSON_GetArrayItem(result, index);
        cJSON *days = NULL;
        cJSON *week = NULL;
        cJSON *citynm = NULL;
        cJSON *weather = NULL;
        cJSON *temperature = NULL;

        if (item == NULL) {
            continue;
        }

        days = cJSON_GetObjectItem(item, "days");
        week = cJSON_GetObjectItem(item, "week");
        citynm = cJSON_GetObjectItem(item, "citynm");
        weather = cJSON_GetObjectItem(item, "weather");
        temperature = cJSON_GetObjectItem(item, "temperature");
        if (!cJSON_IsString(days) ||
            !cJSON_IsString(week) ||
            !cJSON_IsString(citynm) ||
            !cJSON_IsString(weather) ||
            !cJSON_IsString(temperature)) {
            continue;
        }

        weather_copy_field(forecast->days[forecast->count].day,
                           sizeof(forecast->days[forecast->count].day),
                           days->valuestring);
        weather_copy_field(forecast->days[forecast->count].week,
                           sizeof(forecast->days[forecast->count].week),
                           week->valuestring);
        weather_copy_field(forecast->days[forecast->count].city,
                           sizeof(forecast->days[forecast->count].city),
                           citynm->valuestring);
        weather_copy_field(forecast->days[forecast->count].weather,
                           sizeof(forecast->days[forecast->count].weather),
                           weather->valuestring);
        weather_copy_field(forecast->days[forecast->count].temperature,
                           sizeof(forecast->days[forecast->count].temperature),
                           temperature->valuestring);
        ++forecast->count;
    }

    return forecast->count > 0 ? 0 : -1;
}

int weather_parse_http_response(const char *response, weather_forecast_t *forecast)
{
    const char *body = NULL;
    const char *json_start = NULL;
    const char *chunk_end = NULL;
    char *json_buf = NULL;
    size_t json_len = 0;
    cJSON *json = NULL;
    cJSON *success = NULL;
    cJSON *result = NULL;
    int ret = -1;

    if (response == NULL || forecast == NULL) {
        return -1;
    }

    memset(forecast, 0, sizeof(*forecast));
    body = strstr(response, "\r\n\r\n");
    if (body == NULL) {
        return -1;
    }
    body += 4;

    if (strstr(response, "Transfer-Encoding: chunked") != NULL) {
        json_start = weather_skip_chunk_prefix(body);
        if (json_start == NULL) {
            return -1;
        }
        chunk_end = strstr(json_start, "\r\n0\r\n\r\n");
        if (chunk_end == NULL) {
            return -1;
        }
        json_len = (size_t)(chunk_end - json_start);
    } else {
        json_start = body;
        json_len = strlen(json_start);
    }

    json_buf = (char *)malloc(json_len + 1);
    if (json_buf == NULL) {
        return -1;
    }
    memcpy(json_buf, json_start, json_len);
    json_buf[json_len] = '\0';

    json = cJSON_Parse(json_buf);
    if (json == NULL) {
        goto out;
    }

    success = cJSON_GetObjectItem(json, "success");
    result = cJSON_GetObjectItem(json, "result");
    if (!cJSON_IsString(success) || strcmp(success->valuestring, "1") != 0) {
        goto out;
    }

    if (weather_parse_result_array(result, forecast) != 0) {
        goto out;
    }

    forecast->valid = 1;
    forecast->updated_at = time(NULL);
    ret = 0;

out:
    if (json != NULL) {
        cJSON_Delete(json);
    }
    free(json_buf);
    return ret;
}

void weather_snapshot_from_forecast(const weather_forecast_t *forecast, weather_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    if (forecast == NULL || !forecast->valid || forecast->count <= 0) {
        return;
    }

    snapshot->valid = 1;
    weather_copy_field(snapshot->day, sizeof(snapshot->day), forecast->days[0].day);
    weather_copy_field(snapshot->week, sizeof(snapshot->week), forecast->days[0].week);
    weather_copy_field(snapshot->city, sizeof(snapshot->city), forecast->days[0].city);
    weather_copy_field(snapshot->weather, sizeof(snapshot->weather), forecast->days[0].weather);
    weather_copy_field(snapshot->temperature, sizeof(snapshot->temperature), forecast->days[0].temperature);
}

int weather_client_fetch(const char *city, weather_forecast_t *forecast)
{
#ifdef _WIN32
    (void)city;
    (void)forecast;
    return -1;
#else
    int sockfd = -1;
    int ret = -1;
    char response[WEATHER_RESP_BUF_SIZE];

    if (city == NULL || forecast == NULL) {
        return -1;
    }

    sockfd = weather_create_tcp_connection(WEATHER_API_IP, WEATHER_API_PORT);
    if (sockfd == -1) {
        return -1;
    }

    if (weather_send_http_request(sockfd, city) != 0) {
        goto out;
    }

    if (weather_recv_http_response(sockfd, response, sizeof(response)) != 0) {
        goto out;
    }

    ret = weather_parse_http_response(response, forecast);

out:
    close(sockfd);
    return ret;
#endif
}
