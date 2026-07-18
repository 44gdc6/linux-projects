#include "config/web_config.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_FILE_MAX 4096
#define PARSE_INVALID (-1)
#define PARSE_OK 0
#define PARSE_MISSING 1

void web_config_defaults(web_config_t *config)
{
    if (config == NULL) {
        return;
    }
    config->collect_interval = 1;
    snprintf(config->mqtt_mode, sizeof(config->mqtt_mode), "ubuntu-ingest");
    config->alcohol_high = 1800;
    config->motion_delta = 120;
}

static int json_object_complete(const char *json)
{
    const char *p = json;
    int depth = 0;
    int in_string = 0;
    int escaped = 0;

    if (json == NULL) {
        return 0;
    }
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '{') {
        return 0;
    }

    for (; *p != '\0'; p++) {
        if (in_string) {
            if (escaped) {
                escaped = 0;
            } else if (*p == '\\') {
                escaped = 1;
            } else if (*p == '"') {
                in_string = 0;
            } else if ((unsigned char)*p < 0x20) {
                return 0;
            }
            continue;
        }

        if (*p == '"') {
            in_string = 1;
        } else if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            depth--;
            if (depth == 0) {
                p++;
                while (*p != '\0' && isspace((unsigned char)*p)) {
                    p++;
                }
                return *p == '\0';
            }
            if (depth < 0) {
                return 0;
            }
        }
    }
    return 0;
}

static const char *find_key(const char *json, const char *key)
{
    char pattern[64];
    const char *pos;

    if (json == NULL || key == NULL) {
        return NULL;
    }
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    pos = strstr(json, pattern);
    if (pos == NULL) {
        return NULL;
    }
    pos += strlen(pattern);
    while (*pos != '\0' && isspace((unsigned char)*pos)) {
        pos++;
    }
    if (*pos != ':') {
        return NULL;
    }
    pos++;
    while (*pos != '\0' && isspace((unsigned char)*pos)) {
        pos++;
    }
    return pos;
}

static int value_boundary_ok(const char *end)
{
    if (end == NULL) {
        return 0;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    return *end == ',' || *end == '}' || *end == ']';
}

static int parse_int_field(const char *json, const char *key, int *out)
{
    const char *pos = find_key(json, key);
    char *end = NULL;
    long value;

    if (pos == NULL) {
        return PARSE_MISSING;
    }
    if (out == NULL || *pos == '+') {
        return PARSE_INVALID;
    }
    errno = 0;
    value = strtol(pos, &end, 10);
    if (end == pos || errno == ERANGE || value < INT_MIN || value > INT_MAX ||
        !value_boundary_ok(end)) {
        return PARSE_INVALID;
    }
    *out = (int)value;
    return PARSE_OK;
}

static int parse_string_field(const char *json, const char *key,
                              char *out, size_t out_size)
{
    const char *pos = find_key(json, key);
    size_t len = 0;

    if (pos == NULL) {
        return PARSE_MISSING;
    }
    if (out == NULL || out_size == 0 || *pos != '"') {
        return PARSE_INVALID;
    }
    pos++;
    while (*pos != '\0') {
        unsigned char ch = (unsigned char)*pos++;
        if (ch == '"') {
            out[len] = '\0';
            return len > 0 ? PARSE_OK : PARSE_INVALID;
        }
        if (ch == '\\' || ch < 0x20 || len + 1 >= out_size) {
            return PARSE_INVALID;
        }
        out[len++] = (char)ch;
    }
    return PARSE_INVALID;
}

static int valid_mqtt_mode(const char *mode)
{
    size_t i;
    size_t len;

    if (mode == NULL || mode[0] == '\0') {
        return 0;
    }
    len = strlen(mode);
    if (len >= 32) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)mode[i];
        if (!isalnum(ch) && ch != '-' && ch != '_' && ch != '.') {
            return 0;
        }
    }
    return 1;
}

static int validate_config(const web_config_t *config)
{
    if (config == NULL) {
        return -1;
    }
    if (config->collect_interval < 1 || config->collect_interval > 3600) {
        return -1;
    }
    if (config->alcohol_high < 0 || config->alcohol_high > 65535) {
        return -1;
    }
    if (config->motion_delta < 0 || config->motion_delta > 100000) {
        return -1;
    }
    if (!valid_mqtt_mode(config->mqtt_mode)) {
        return -1;
    }
    return 0;
}

int web_config_apply_json(web_config_t *config, const char *json)
{
    web_config_t next;
    int rc;
    int seen = 0;

    if (config == NULL || json == NULL || !json_object_complete(json)) {
        return -1;
    }
    next = *config;

    rc = parse_int_field(json, "collect_interval", &next.collect_interval);
    if (rc == PARSE_INVALID) return -1;
    if (rc == PARSE_OK) seen = 1;

    rc = parse_string_field(json, "mode", next.mqtt_mode, sizeof(next.mqtt_mode));
    if (rc == PARSE_INVALID) return -1;
    if (rc == PARSE_OK) seen = 1;

    rc = parse_int_field(json, "alcohol_high", &next.alcohol_high);
    if (rc == PARSE_INVALID) return -1;
    if (rc == PARSE_OK) seen = 1;

    rc = parse_int_field(json, "motion_delta", &next.motion_delta);
    if (rc == PARSE_INVALID) return -1;
    if (rc == PARSE_OK) seen = 1;

    if (!seen || validate_config(&next) != 0) {
        return -1;
    }
    *config = next;
    return 0;
}

int web_config_to_json(const web_config_t *config, char *buffer, size_t buffer_size)
{
    int n;

    if (config == NULL || buffer == NULL || buffer_size == 0 ||
        validate_config(config) != 0) {
        return -1;
    }
    n = snprintf(buffer, buffer_size,
                 "{\"collect_interval\":%d,"
                 "\"mqtt\":{\"mode\":\"%s\"},"
                 "\"alarm_thresholds\":{\"alcohol_high\":%d,\"motion_delta\":%d},"
                 "\"downlink\":{\"enabled\":false,\"status\":\"local-only\"}}",
                 config->collect_interval,
                 config->mqtt_mode,
                 config->alcohol_high,
                 config->motion_delta);
    if (n < 0 || (size_t)n >= buffer_size) {
        return -1;
    }
    return 0;
}

int web_config_load(web_config_t *config, const char *path)
{
    FILE *fp;
    char buffer[CONFIG_FILE_MAX + 1];
    size_t n;

    if (config == NULL) {
        return -1;
    }
    web_config_defaults(config);
    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return errno == ENOENT ? 0 : -1;
    }
    n = fread(buffer, 1, CONFIG_FILE_MAX, fp);
    if (ferror(fp) || !feof(fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    buffer[n] = '\0';
    if (n == 0) {
        return 0;
    }
    return web_config_apply_json(config, buffer);
}

int web_config_save(const web_config_t *config, const char *path)
{
    char json[512];
    char tmp_path[512];
    FILE *fp;
    int close_rc;

    if (path == NULL || path[0] == '\0' ||
        web_config_to_json(config, json, sizeof(json)) != 0) {
        return -1;
    }
    if (strlen(path) + 5 > sizeof(tmp_path) ||
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path) < 0) {
        return -1;
    }

    fp = fopen(tmp_path, "wb");
    if (fp == NULL) {
        return -1;
    }
    if (fputs(json, fp) < 0 || fputc('\n', fp) == EOF) {
        fclose(fp);
        remove(tmp_path);
        return -1;
    }
    close_rc = fclose(fp);
    if (close_rc != 0) {
        remove(tmp_path);
        return -1;
    }

#ifdef _WIN32
    remove(path);
#endif
    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return -1;
    }
    return 0;
}
