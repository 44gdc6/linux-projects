#include "ingest/payload_parser.h"
#include "ingest/realtime_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "assert failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

#define ASSERT_INT(expected, actual) do { \
    if ((expected) != (actual)) { \
        fprintf(stderr, "assert failed at %s:%d: expected %d got %d\n", \
                __FILE__, __LINE__, (expected), (actual)); \
        return 1; \
    } \
} while (0)

#define ASSERT_DOUBLE(expected, actual) do { \
    double diff = (expected) - (actual); \
    if (diff < 0) diff = -diff; \
    if (diff > 0.0001) { \
        fprintf(stderr, "assert failed at %s:%d: expected %.4f got %.4f\n", \
                __FILE__, __LINE__, (expected), (actual)); \
        return 1; \
    } \
} while (0)

static int test_parse_flat_payload(void)
{
    const char *payload =
        "{"
        "\"device\":\"device_01\","
        "\"timestamp\":1719849600,"
        "\"temperature\":25.6,"
        "\"humidity\":55.0,"
        "\"alcohol_raw\":980,"
        "\"alcohol_level\":0,"
        "\"alcohol_alarm\":false,"
        "\"accel_x\":12,"
        "\"accel_y\":-8,"
        "\"accel_z\":981,"
        "\"motion_state\":0,"
        "\"motion_alarm\":true,"
        "\"flame_status\":0,"
        "\"flame_valid\":true,"
        "\"dht11_temperature\":26,"
        "\"dht11_humidity\":55,"
        "\"dht11_valid\":true,"
        "\"gps_lat\":39.9042,"
        "\"gps_lon\":116.4074,"
        "\"gps_speed\":60.5,"
        "\"gps_satellites\":8,"
        "\"gps_fence_alarm\":false"
        "}";
    device_sample_t sample;

    memset(&sample, 0, sizeof(sample));
    ASSERT_INT(0, payload_parse_json(payload, &sample));
    ASSERT_TRUE(strcmp(sample.device, "device_01") == 0);
    ASSERT_TRUE(sample.timestamp == 1719849600ULL);
    ASSERT_DOUBLE(25.6, sample.temperature);
    ASSERT_DOUBLE(55.0, sample.humidity);
    ASSERT_INT(980, sample.alcohol_raw);
    ASSERT_INT(1, sample.motion_alarm);
    ASSERT_INT(1, sample.flame_valid);
    ASSERT_INT(26, sample.dht11_temperature);
    ASSERT_DOUBLE(39.9042, sample.gps_lat);
    ASSERT_DOUBLE(116.4074, sample.gps_lon);
    ASSERT_DOUBLE(60.5, sample.gps_speed);
    ASSERT_INT(8, sample.gps_satellites);

    return 0;
}

static int test_cache_json_contains_current_status(void)
{
    realtime_cache_t cache;
    device_sample_t sample;
    char json[4096];

    realtime_cache_init(&cache);
    memset(&sample, 0, sizeof(sample));
    strcpy(sample.device, "device_01");
    sample.timestamp = 1719849600ULL;
    sample.temperature = 25.6;
    sample.humidity = 55.0;
    sample.alcohol_raw = 980;
    sample.accel_x = 12;
    sample.accel_y = -8;
    sample.accel_z = 981;
    sample.motion_alarm = 1;
    sample.gps_lat = 39.9042;
    sample.gps_lon = 116.4074;
    sample.gps_speed = 60.5f;
    sample.gps_satellites = 8;

    realtime_cache_update(&cache, &sample);
    ASSERT_INT(0, realtime_cache_to_json(&cache, json, sizeof(json)));

    ASSERT_TRUE(strstr(json, "\"device\":\"device_01\"") != NULL);
    ASSERT_TRUE(strstr(json, "\"online\":true") != NULL);
    ASSERT_TRUE(strstr(json, "\"temperature\":25.60") != NULL);
    ASSERT_TRUE(strstr(json, "\"motion_alarm\":true") != NULL);
    ASSERT_TRUE(strstr(json, "\"lat\":39.904200") != NULL);
    ASSERT_TRUE(strstr(json, "\"satellites\":8") != NULL);

    realtime_cache_destroy(&cache);
    return 0;
}

static int test_parse_onenet_property_payload(void)
{
    const char *payload =
        "{"
        "\"id\":\"10001\","
        "\"params\":{"
        "\"tempval\":{\"value\":26},"
        "\"humval\":{\"value\":58},"
        "\"aclval\":{\"value\":1100},"
        "\"accx\":{\"value\":4},"
        "\"accy\":{\"value\":5},"
        "\"accz\":{\"value\":980},"
        "\"motionst\":{\"value\":1},"
        "\"flamest\":{\"value\":0},"
        "\"dht11hum\":{\"value\":61},"
        "\"dht11temp\":{\"value\":27},"
        "\"gpslat\":{\"value\":34.2614},"
        "\"gpslon\":{\"value\":108.9404},"
        "\"gpsspd\":{\"value\":12.5},"
        "\"gpssat\":{\"value\":7},"
        "\"gpsfence\":{\"value\":0}"
        "}}";
    device_sample_t sample;

    ASSERT_INT(0, payload_parse_json(payload, &sample));
    ASSERT_DOUBLE(26.0, sample.temperature);
    ASSERT_DOUBLE(58.0, sample.humidity);
    ASSERT_INT(1100, sample.alcohol_raw);
    ASSERT_INT(4, sample.accel_x);
    ASSERT_INT(5, sample.accel_y);
    ASSERT_INT(980, sample.accel_z);
    ASSERT_INT(1, sample.motion_state);
    ASSERT_INT(0, sample.flame_status);
    ASSERT_INT(61, sample.dht11_humidity);
    ASSERT_INT(27, sample.dht11_temperature);
    ASSERT_DOUBLE(34.2614, sample.gps_lat);
    ASSERT_DOUBLE(108.9404, sample.gps_lon);
    ASSERT_DOUBLE(12.5, sample.gps_speed);
    ASSERT_INT(7, sample.gps_satellites);
    return 0;
}

static int test_reject_invalid_and_truncated_json(void)
{
    device_sample_t sample;

    ASSERT_INT(-1, payload_parse_json("{\"temperature\":25,}", &sample));
    ASSERT_INT(-1, payload_parse_json("{\"temperature\":25", &sample));
    ASSERT_INT(-1, payload_parse_json("{\"temperature\":25,\"motion_alarm\":truex}", &sample));

    return 0;
}

static int test_reject_payload_without_telemetry_fields(void)
{
    device_sample_t sample;

    ASSERT_INT(-1, payload_parse_json("{\"device\":\"device_01\",\"timestamp\":1719849600}", &sample));
    ASSERT_INT(-1, payload_parse_json("{\"params\":{}}", &sample));

    return 0;
}

static int test_parse_scoped_onenet_property_value(void)
{
    const char *payload =
        "{"
        "\"params\":{"
        "\"tempval\":{\"meta\":{\"value\":99},\"value\":26},"
        "\"humval\":{\"value\":58}"
        "}"
        "}";
    device_sample_t sample;

    ASSERT_INT(0, payload_parse_json(payload, &sample));
    ASSERT_DOUBLE(26.0, sample.temperature);
    ASSERT_DOUBLE(58.0, sample.humidity);

    return 0;
}

static int test_device_string_is_truncated_and_terminated(void)
{
    char device[96];
    char payload[192];
    device_sample_t sample;
    size_t i;

    for (i = 0; i + 1 < sizeof(device); i++) {
        device[i] = (char)('A' + (i % 26));
    }
    device[sizeof(device) - 1] = '\0';

    snprintf(payload, sizeof(payload),
             "{\"device\":\"%s\",\"temperature\":24.5}", device);

    ASSERT_INT(0, payload_parse_json(payload, &sample));
    ASSERT_INT((int)sizeof(sample.device) - 1, (int)strlen(sample.device));
    ASSERT_TRUE(strncmp(sample.device, device, sizeof(sample.device) - 1) == 0);
    ASSERT_TRUE(sample.device[sizeof(sample.device) - 1] == '\0');

    return 0;
}

static int test_parse_json_and_numeric_booleans(void)
{
    const char *payload =
        "{"
        "\"temperature\":20,"
        "\"alcohol_alarm\":1,"
        "\"motion_alarm\":0,"
        "\"flame_valid\":true,"
        "\"dht11_valid\":false,"
        "\"gps_fence_alarm\":1"
        "}";
    device_sample_t sample;

    ASSERT_INT(0, payload_parse_json(payload, &sample));
    ASSERT_INT(1, sample.alcohol_alarm);
    ASSERT_INT(0, sample.motion_alarm);
    ASSERT_INT(1, sample.flame_valid);
    ASSERT_INT(0, sample.dht11_valid);
    ASSERT_INT(1, sample.gps_fence_alarm);

    return 0;
}

static int test_reject_invalid_boolean_values(void)
{
    device_sample_t sample;

    ASSERT_INT(-1, payload_parse_json("{\"temperature\":20,\"motion_alarm\":2}", &sample));
    ASSERT_INT(-1, payload_parse_json("{\"temperature\":20,\"gps_fence_alarm\":-1}", &sample));

    return 0;
}

static int test_reject_numeric_edge_cases(void)
{
    device_sample_t sample;

    ASSERT_INT(-1, payload_parse_json("{\"temperature\":1e309}", &sample));
    ASSERT_INT(-1, payload_parse_json("{\"temperature\":20,\"timestamp\":-1}", &sample));
    ASSERT_INT(-1, payload_parse_json("{\"temperature\":20,\"timestamp\":9223372036854775808}", &sample));
    ASSERT_INT(-1, payload_parse_json("{\"accel_x\":2147483648}", &sample));
    ASSERT_INT(-1, payload_parse_json("{\"temperature\":20,\"humidity\":25abc}", &sample));

    return 0;
}

int main(void)
{
    if (test_parse_flat_payload() != 0) {
        return 1;
    }
    if (test_parse_onenet_property_payload() != 0) {
        return 1;
    }
    if (test_reject_invalid_and_truncated_json() != 0) {
        return 1;
    }
    if (test_reject_payload_without_telemetry_fields() != 0) {
        return 1;
    }
    if (test_parse_scoped_onenet_property_value() != 0) {
        return 1;
    }
    if (test_device_string_is_truncated_and_terminated() != 0) {
        return 1;
    }
    if (test_parse_json_and_numeric_booleans() != 0) {
        return 1;
    }
    if (test_reject_invalid_boolean_values() != 0) {
        return 1;
    }
    if (test_reject_numeric_edge_cases() != 0) {
        return 1;
    }
    if (test_cache_json_contains_current_status() != 0) {
        return 1;
    }
    puts("payload/cache tests passed");
    return 0;
}
