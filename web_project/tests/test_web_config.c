#include "config/web_config.h"

#include <stdio.h>
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

static int test_defaults_and_json(void)
{
    web_config_t config;
    char json[512];

    web_config_defaults(&config);
    ASSERT_INT(1, config.collect_interval);
    ASSERT_INT(1800, config.alcohol_high);
    ASSERT_INT(120, config.motion_delta);
    ASSERT_TRUE(strcmp(config.mqtt_mode, "ubuntu-ingest") == 0);
    ASSERT_INT(0, web_config_to_json(&config, json, sizeof(json)));
    ASSERT_TRUE(strstr(json, "\"collect_interval\":1") != NULL);
    ASSERT_TRUE(strstr(json, "\"status\":\"local-only\"") != NULL);
    return 0;
}

static int test_apply_valid_update(void)
{
    web_config_t config;

    web_config_defaults(&config);
    ASSERT_INT(0, web_config_apply_json(&config,
        "{\"collect_interval\":5,\"mqtt\":{\"mode\":\"ubuntu-ingest\"},"
        "\"alarm_thresholds\":{\"alcohol_high\":2200,\"motion_delta\":300}}"));
    ASSERT_INT(5, config.collect_interval);
    ASSERT_INT(2200, config.alcohol_high);
    ASSERT_INT(300, config.motion_delta);
    ASSERT_TRUE(strcmp(config.mqtt_mode, "ubuntu-ingest") == 0);
    return 0;
}

static int test_reject_invalid_update(void)
{
    web_config_t config;

    web_config_defaults(&config);
    ASSERT_INT(-1, web_config_apply_json(&config, "{\"collect_interval\":0}"));
    ASSERT_INT(-1, web_config_apply_json(&config, "{\"alcohol_high\":70000}"));
    ASSERT_INT(-1, web_config_apply_json(&config, "{\"motion_delta\":-1}"));
    ASSERT_INT(-1, web_config_apply_json(&config, "{\"mqtt\":{\"mode\":\"bad mode\"}}"));
    ASSERT_INT(-1, web_config_apply_json(&config, "{\"collect_interval\":5}x"));
    ASSERT_INT(-1, web_config_apply_json(&config, "{\"unknown\":1}"));
    return 0;
}

static int test_save_and_load_roundtrip(void)
{
    const char *path = "tests/web_config_test.json";
    web_config_t config;
    web_config_t loaded;

    remove(path);
    web_config_defaults(&config);
    ASSERT_INT(0, web_config_apply_json(&config,
        "{\"collect_interval\":9,\"alarm_thresholds\":{\"alcohol_high\":2100,\"motion_delta\":88}}"));
    ASSERT_INT(0, web_config_save(&config, path));
    ASSERT_INT(0, web_config_load(&loaded, path));
    ASSERT_INT(9, loaded.collect_interval);
    ASSERT_INT(2100, loaded.alcohol_high);
    ASSERT_INT(88, loaded.motion_delta);
    ASSERT_TRUE(strcmp(loaded.mqtt_mode, "ubuntu-ingest") == 0);
    remove(path);
    return 0;
}

int main(void)
{
    if (test_defaults_and_json() != 0) return 1;
    if (test_apply_valid_update() != 0) return 1;
    if (test_reject_invalid_update() != 0) return 1;
    if (test_save_and_load_roundtrip() != 0) return 1;
    puts("web config tests passed");
    return 0;
}
