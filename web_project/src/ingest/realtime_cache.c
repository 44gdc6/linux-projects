#include "ingest/realtime_cache.h"

#include <stdio.h>
#include <string.h>

static void json_escape_string(const char *input, char *output, size_t output_size)
{
    size_t len = 0;

    if (output == NULL || output_size == 0) {
        return;
    }
    if (input == NULL) {
        input = "";
    }

    for (; *input != '\0' && len + 1 < output_size; input++) {
        unsigned char ch = (unsigned char)*input;
        const char *escaped = NULL;
        char unicode_escape[7];

        switch (ch) {
        case '"': escaped = "\\\""; break;
        case '\\': escaped = "\\\\"; break;
        case '\b': escaped = "\\b"; break;
        case '\f': escaped = "\\f"; break;
        case '\n': escaped = "\\n"; break;
        case '\r': escaped = "\\r"; break;
        case '\t': escaped = "\\t"; break;
        default:
            if (ch < 0x20) {
                snprintf(unicode_escape, sizeof(unicode_escape), "\\u%04x", ch);
                escaped = unicode_escape;
            }
            break;
        }

        if (escaped != NULL) {
            size_t escaped_len = strlen(escaped);
            if (len + escaped_len >= output_size) {
                break;
            }
            memcpy(output + len, escaped, escaped_len);
            len += escaped_len;
        } else {
            output[len++] = (char)ch;
        }
    }
    output[len] = '\0';
}

int realtime_cache_init(realtime_cache_t *cache)
{
    if (cache == NULL) {
        return -1;
    }

    memset(cache, 0, sizeof(*cache));
    return pthread_mutex_init(&cache->lock, NULL);
}

void realtime_cache_destroy(realtime_cache_t *cache)
{
    if (cache != NULL) {
        pthread_mutex_destroy(&cache->lock);
    }
}

void realtime_cache_update(realtime_cache_t *cache, const device_sample_t *sample)
{
    if (cache == NULL || sample == NULL) {
        return;
    }

    pthread_mutex_lock(&cache->lock);
    cache->sample = *sample;
    cache->has_sample = 1;
    pthread_mutex_unlock(&cache->lock);
}

int realtime_cache_copy(realtime_cache_t *cache, device_sample_t *out)
{
    if (cache == NULL || out == NULL) {
        return -1;
    }

    pthread_mutex_lock(&cache->lock);
    if (!cache->has_sample) {
        pthread_mutex_unlock(&cache->lock);
        return -1;
    }
    *out = cache->sample;
    pthread_mutex_unlock(&cache->lock);
    return 0;
}

int realtime_cache_to_json(realtime_cache_t *cache, char *buffer, size_t buffer_size)
{
    device_sample_t s;
    char escaped_device[256];

    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }

    if (realtime_cache_copy(cache, &s) != 0) {
        snprintf(buffer, buffer_size,
                 "{\"online\":false,\"message\":\"no telemetry received\"}");
        return 0;
    }

    json_escape_string(s.device, escaped_device, sizeof(escaped_device));
    snprintf(buffer, buffer_size,
             "{"
             "\"timestamp\":%llu,"
             "\"device\":\"%s\","
             "\"online\":true,"
             "\"sensors\":{"
             "\"cargo\":{"
             "\"temperature\":%.2f,"
             "\"vibration\":{\"accel_x\":%d,\"accel_y\":%d,\"accel_z\":%d,"
             "\"motion_state\":%d,\"motion_alarm\":%s},"
             "\"flame\":{\"valid\":%s,\"status\":%d}"
             "},"
             "\"cabin\":{"
             "\"alcohol_raw\":%d,\"alcohol_level\":%d,\"alcohol_alarm\":%s,"
             "\"humidity\":%.2f,"
             "\"gps\":{\"lat\":%.6f,\"lon\":%.6f,\"speed\":%.2f,"
             "\"satellites\":%d,\"fence_alarm\":%s}"
             "}"
             "},"
             "\"can_nodes\":["
             "{\"id\":1,\"name\":\"STM32F103\",\"online\":%s,\"flame_status\":%d},"
             "{\"id\":2,\"name\":\"STM32F407\",\"online\":%s,"
             "\"dht11_temperature\":%d,\"dht11_humidity\":%d}"
             "]"
             "}",
             (unsigned long long)s.timestamp,
             escaped_device,
             s.temperature,
             s.accel_x,
             s.accel_y,
             s.accel_z,
             s.motion_state,
             s.motion_alarm ? "true" : "false",
             s.flame_valid ? "true" : "false",
             s.flame_status,
             s.alcohol_raw,
             s.alcohol_level,
             s.alcohol_alarm ? "true" : "false",
             s.humidity,
             s.gps_lat,
             s.gps_lon,
             s.gps_speed,
             s.gps_satellites,
             s.gps_fence_alarm ? "true" : "false",
             s.flame_valid ? "true" : "false",
             s.flame_status,
             s.dht11_valid ? "true" : "false",
             s.dht11_temperature,
             s.dht11_humidity);

    return 0;
}
