#include "ingest/payload_parser.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PARSE_INVALID (-1)
#define PARSE_OK 0
#define PARSE_MISSING 1
#define SQLITE_INT64_MAX_U64 9223372036854775807ULL

static void skip_json_ws(const char **cursor)
{
    while (**cursor != '\0' && isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }
}

static int parse_json_value(const char **cursor);

static int parse_json_string_token(const char **cursor)
{
    const char *pos = *cursor;

    if (*pos != '"') {
        return 0;
    }
    pos++;

    while (*pos != '\0') {
        unsigned char ch = (unsigned char)*pos++;

        if (ch == '"') {
            *cursor = pos;
            return 1;
        }
        if (ch == '\\') {
            int i;

            ch = (unsigned char)*pos++;
            switch (ch) {
            case '"':
            case '\\':
            case '/':
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
                break;
            case 'u':
                for (i = 0; i < 4; i++) {
                    if (!isxdigit((unsigned char)pos[i])) {
                        return 0;
                    }
                }
                pos += 4;
                break;
            default:
                return 0;
            }
        } else if (ch < 0x20) {
            return 0;
        }
    }

    return 0;
}

static int parse_json_number_token(const char **cursor)
{
    const char *pos = *cursor;

    if (*pos == '-') {
        pos++;
    }
    if (!isdigit((unsigned char)*pos)) {
        return 0;
    }
    if (*pos == '0') {
        pos++;
        if (isdigit((unsigned char)*pos)) {
            return 0;
        }
    } else {
        while (isdigit((unsigned char)*pos)) {
            pos++;
        }
    }
    if (*pos == '.') {
        pos++;
        if (!isdigit((unsigned char)*pos)) {
            return 0;
        }
        while (isdigit((unsigned char)*pos)) {
            pos++;
        }
    }
    if (*pos == 'e' || *pos == 'E') {
        pos++;
        if (*pos == '+' || *pos == '-') {
            pos++;
        }
        if (!isdigit((unsigned char)*pos)) {
            return 0;
        }
        while (isdigit((unsigned char)*pos)) {
            pos++;
        }
    }

    *cursor = pos;
    return 1;
}

static int parse_json_literal_token(const char **cursor, const char *literal)
{
    size_t len = strlen(literal);

    if (strncmp(*cursor, literal, len) != 0) {
        return 0;
    }

    *cursor += len;
    return 1;
}

static int parse_json_array_token(const char **cursor)
{
    const char *pos = *cursor;

    if (*pos != '[') {
        return 0;
    }
    pos++;
    skip_json_ws(&pos);
    if (*pos == ']') {
        *cursor = pos + 1;
        return 1;
    }

    while (*pos != '\0') {
        if (!parse_json_value(&pos)) {
            return 0;
        }
        skip_json_ws(&pos);
        if (*pos == ',') {
            pos++;
            skip_json_ws(&pos);
            if (*pos == ']') {
                return 0;
            }
            continue;
        }
        if (*pos == ']') {
            *cursor = pos + 1;
            return 1;
        }
        return 0;
    }

    return 0;
}

static int parse_json_object_token(const char **cursor)
{
    const char *pos = *cursor;

    if (*pos != '{') {
        return 0;
    }
    pos++;
    skip_json_ws(&pos);
    if (*pos == '}') {
        *cursor = pos + 1;
        return 1;
    }

    while (*pos != '\0') {
        if (!parse_json_string_token(&pos)) {
            return 0;
        }
        skip_json_ws(&pos);
        if (*pos != ':') {
            return 0;
        }
        pos++;
        skip_json_ws(&pos);
        if (!parse_json_value(&pos)) {
            return 0;
        }
        skip_json_ws(&pos);
        if (*pos == ',') {
            pos++;
            skip_json_ws(&pos);
            if (*pos == '}') {
                return 0;
            }
            continue;
        }
        if (*pos == '}') {
            *cursor = pos + 1;
            return 1;
        }
        return 0;
    }

    return 0;
}

static int parse_json_value(const char **cursor)
{
    skip_json_ws(cursor);
    if (**cursor == '{') {
        return parse_json_object_token(cursor);
    }
    if (**cursor == '[') {
        return parse_json_array_token(cursor);
    }
    if (**cursor == '"') {
        return parse_json_string_token(cursor);
    }
    if (**cursor == '-' || isdigit((unsigned char)**cursor)) {
        return parse_json_number_token(cursor);
    }
    if (parse_json_literal_token(cursor, "true")) {
        return 1;
    }
    if (parse_json_literal_token(cursor, "false")) {
        return 1;
    }
    if (parse_json_literal_token(cursor, "null")) {
        return 1;
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

static int json_object_is_complete(const char *json)
{
    const char *pos;

    if (json == NULL) {
        return 0;
    }

    pos = json;
    skip_json_ws(&pos);
    if (!parse_json_object_token(&pos)) {
        return 0;
    }
    skip_json_ws(&pos);
    return *pos == '\0';
}

static const char *matching_object_end(const char *object_start)
{
    const char *pos;
    int depth = 0;
    int in_string = 0;
    int escaped = 0;

    if (object_start == NULL || *object_start != '{') {
        return NULL;
    }

    for (pos = object_start; *pos != '\0'; pos++) {
        if (in_string) {
            if (escaped) {
                escaped = 0;
            } else if (*pos == '\\') {
                escaped = 1;
            } else if (*pos == '"') {
                in_string = 0;
            }
            continue;
        }

        if (*pos == '"') {
            in_string = 1;
        } else if (*pos == '{') {
            depth++;
        } else if (*pos == '}') {
            depth--;
            if (depth == 0) {
                return pos;
            }
            if (depth < 0) {
                return NULL;
            }
        }
    }

    return NULL;
}

static int value_boundary_ok(const char *end, const char *limit)
{
    if (end == NULL) {
        return 0;
    }

    if (limit != NULL) {
        while (end <= limit && isspace((unsigned char)*end)) {
            end++;
        }
        return end <= limit && (*end == ',' || *end == '}' || *end == ']');
    }

    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    return *end == '\0' || *end == ',' || *end == '}' || *end == ']';
}

static int json_key_token_matches(const char *token_start, const char *token_end,
                                  const char *key)
{
    const char *pos = token_start + 1;

    while (pos < token_end - 1 && *key != '\0') {
        if (*pos == '\\' || *pos != *key) {
            return 0;
        }
        pos++;
        key++;
    }

    return pos == token_end - 1 && *key == '\0';
}

static const char *find_direct_object_value(const char *object_start,
                                            const char *object_end,
                                            const char *key)
{
    const char *pos;

    if (object_start == NULL || object_end == NULL || key == NULL ||
        *object_start != '{' || object_end <= object_start) {
        return NULL;
    }

    pos = object_start + 1;
    while (pos < object_end) {
        const char *key_start;
        const char *key_end;
        const char *value_start;
        const char *value_end;

        skip_json_ws(&pos);
        if (pos >= object_end || *pos == '}') {
            break;
        }
        key_start = pos;
        if (!parse_json_string_token(&pos) || pos > object_end) {
            return NULL;
        }
        key_end = pos;
        skip_json_ws(&pos);
        if (pos >= object_end || *pos != ':') {
            return NULL;
        }
        pos++;
        skip_json_ws(&pos);
        if (pos > object_end) {
            return NULL;
        }
        value_start = pos;
        value_end = pos;
        if (!parse_json_value(&value_end) || value_end > object_end + 1) {
            return NULL;
        }
        if (json_key_token_matches(key_start, key_end, key)) {
            return value_start;
        }
        pos = value_end;
        skip_json_ws(&pos);
        if (pos < object_end && *pos == ',') {
            pos++;
            continue;
        }
        if (pos == object_end) {
            break;
        }
        return NULL;
    }

    return NULL;
}

static int parse_double_value(const char *pos, const char *limit, double *out)
{
    char *end = NULL;
    double value;

    if (pos == NULL || out == NULL) {
        return PARSE_INVALID;
    }

    errno = 0;
    value = strtod(pos, &end);
    if (end == pos || errno == ERANGE || !isfinite(value) ||
        !value_boundary_ok(end, limit)) {
        return PARSE_INVALID;
    }

    *out = value;
    return PARSE_OK;
}

static int parse_int_value(const char *pos, const char *limit, int *out)
{
    char *end = NULL;
    long value;

    if (pos == NULL || out == NULL || *pos == '+') {
        return PARSE_INVALID;
    }

    errno = 0;
    value = strtol(pos, &end, 10);
    if (end == pos || errno == ERANGE || value < INT_MIN || value > INT_MAX ||
        !value_boundary_ok(end, limit)) {
        return PARSE_INVALID;
    }

    *out = (int)value;
    return PARSE_OK;
}

static int parse_u64_value(const char *pos, const char *limit, uint64_t *out)
{
    char *end = NULL;
    unsigned long long value;

    if (pos == NULL || out == NULL || *pos == '-' || *pos == '+') {
        return PARSE_INVALID;
    }

    errno = 0;
    value = strtoull(pos, &end, 10);
    if (end == pos || errno == ERANGE || !value_boundary_ok(end, limit)) {
        return PARSE_INVALID;
    }

    *out = (uint64_t)value;
    return PARSE_OK;
}

static int parse_string_field(const char *json, const char *key, char *out, size_t out_size)
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
        unsigned char ch = (unsigned char)*pos;

        if (ch == '"') {
            out[len] = '\0';
            return len > 0 ? PARSE_OK : PARSE_INVALID;
        }
        if (ch == '\\') {
            pos++;
            if (*pos == '\0') {
                return PARSE_INVALID;
            }
            switch (*pos) {
            case '"':
            case '\\':
            case '/':
                ch = (unsigned char)*pos;
                break;
            case 'b':
                ch = '\b';
                break;
            case 'f':
                ch = '\f';
                break;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            default:
                return PARSE_INVALID;
            }
        } else if (ch < 0x20) {
            return PARSE_INVALID;
        }
        if (len + 1 < out_size) {
            out[len++] = (char)ch;
        }
        pos++;
    }

    return PARSE_INVALID;
}

static int parse_double_field(const char *json, const char *key, double *out)
{
    const char *pos = find_key(json, key);

    if (pos == NULL) {
        return PARSE_MISSING;
    }

    return parse_double_value(pos, NULL, out);
}

static int parse_property_double_field(const char *json, const char *key, double *out)
{
    const char *pos = find_key(json, key);
    const char *object_end;
    const char *value_pos;

    if (pos == NULL) {
        return PARSE_MISSING;
    }
    if (out == NULL || *pos != '{') {
        return PARSE_INVALID;
    }

    object_end = matching_object_end(pos);
    if (object_end == NULL) {
        return PARSE_INVALID;
    }
    value_pos = find_direct_object_value(pos, object_end, "value");
    if (value_pos == NULL) {
        return PARSE_INVALID;
    }

    return parse_double_value(value_pos, object_end, out);
}

static int parse_property_int_field(const char *json, const char *key, int *out)
{
    const char *pos = find_key(json, key);
    const char *object_end;
    const char *value_pos;

    if (pos == NULL) {
        return PARSE_MISSING;
    }
    if (out == NULL || *pos != '{') {
        return PARSE_INVALID;
    }

    object_end = matching_object_end(pos);
    if (object_end == NULL) {
        return PARSE_INVALID;
    }
    value_pos = find_direct_object_value(pos, object_end, "value");
    if (value_pos == NULL) {
        return PARSE_INVALID;
    }

    return parse_int_value(value_pos, object_end, out);
}

static int parse_int_field(const char *json, const char *key, int *out)
{
    const char *pos = find_key(json, key);

    if (pos == NULL) {
        return PARSE_MISSING;
    }

    return parse_int_value(pos, NULL, out);
}

static int parse_u64_field(const char *json, const char *key, uint64_t *out)
{
    const char *pos = find_key(json, key);

    if (pos == NULL) {
        return PARSE_MISSING;
    }

    return parse_u64_value(pos, NULL, out);
}

static int parse_bool_field(const char *json, const char *key, int *out)
{
    const char *pos = find_key(json, key);
    int value;

    if (pos == NULL) {
        return PARSE_MISSING;
    }
    if (out == NULL) {
        return PARSE_INVALID;
    }

    if (strncmp(pos, "true", 4) == 0 && value_boundary_ok(pos + 4, NULL)) {
        *out = 1;
        return PARSE_OK;
    }
    if (strncmp(pos, "false", 5) == 0 && value_boundary_ok(pos + 5, NULL)) {
        *out = 0;
        return PARSE_OK;
    }

    if (parse_int_value(pos, NULL, &value) != PARSE_OK || (value != 0 && value != 1)) {
        return PARSE_INVALID;
    }

    *out = value;
    return PARSE_OK;
}

static int parse_property_bool_field(const char *json, const char *key, int *out)
{
    const char *pos = find_key(json, key);
    const char *object_end;
    const char *value_pos;
    int value;

    if (pos == NULL) {
        return PARSE_MISSING;
    }
    if (out == NULL || *pos != '{') {
        return PARSE_INVALID;
    }

    object_end = matching_object_end(pos);
    if (object_end == NULL) {
        return PARSE_INVALID;
    }
    value_pos = find_direct_object_value(pos, object_end, "value");
    if (value_pos == NULL) {
        return PARSE_INVALID;
    }

    if (strncmp(value_pos, "true", 4) == 0 && value_boundary_ok(value_pos + 4, object_end)) {
        *out = 1;
        return PARSE_OK;
    }
    if (strncmp(value_pos, "false", 5) == 0 && value_boundary_ok(value_pos + 5, object_end)) {
        *out = 0;
        return PARSE_OK;
    }
    if (parse_int_value(value_pos, object_end, &value) != PARSE_OK || (value != 0 && value != 1)) {
        return PARSE_INVALID;
    }

    *out = value;
    return PARSE_OK;
}

int payload_parse_json(const char *payload, device_sample_t *sample)
{
    double gps_speed = 0.0;
    double tmp = 0.0;
    int parsed_fields = 0;

    if (payload == NULL || sample == NULL) {
        return -1;
    }
    if (!json_object_is_complete(payload)) {
        return -1;
    }

    memset(sample, 0, sizeof(*sample));
    snprintf(sample->device, sizeof(sample->device), "device_01");
    snprintf(sample->raw_payload, sizeof(sample->raw_payload), "%s", payload);
    sample->timestamp = (uint64_t)time(NULL);

#define CHECK_OPTIONAL(call) do { \
    int rc__ = (call); \
    if (rc__ == PARSE_INVALID) { \
        return -1; \
    } \
} while (0)

#define COUNT_OPTIONAL(call) do { \
    int rc__ = (call); \
    if (rc__ == PARSE_OK) { \
        parsed_fields++; \
    } else if (rc__ == PARSE_INVALID) { \
        return -1; \
    } \
} while (0)

#define COUNT_OPTIONAL_ASSIGN(call, assignment) do { \
    int rc__ = (call); \
    if (rc__ == PARSE_OK) { \
        assignment; \
        parsed_fields++; \
    } else if (rc__ == PARSE_INVALID) { \
        return -1; \
    } \
} while (0)

#define COUNT_OPTIONAL_FLOAT(call, value) do { \
    int rc__ = (call); \
    if (rc__ == PARSE_OK) { \
        if ((value) > FLT_MAX || (value) < -FLT_MAX) { \
            return -1; \
        } \
        sample->gps_speed = (float)(value); \
        parsed_fields++; \
    } else if (rc__ == PARSE_INVALID) { \
        return -1; \
    } \
} while (0)

    CHECK_OPTIONAL(parse_string_field(payload, "device", sample->device, sizeof(sample->device)));
    CHECK_OPTIONAL(parse_u64_field(payload, "timestamp", &sample->timestamp));
    if (sample->timestamp > SQLITE_INT64_MAX_U64) {
        return -1;
    }
    COUNT_OPTIONAL(parse_double_field(payload, "temperature", &sample->temperature));
    COUNT_OPTIONAL(parse_double_field(payload, "temp", &sample->temperature));
    COUNT_OPTIONAL_ASSIGN(parse_property_double_field(payload, "tempval", &tmp),
                          sample->temperature = tmp);
    COUNT_OPTIONAL(parse_double_field(payload, "humidity", &sample->humidity));
    COUNT_OPTIONAL(parse_double_field(payload, "hum", &sample->humidity));
    COUNT_OPTIONAL_ASSIGN(parse_property_double_field(payload, "humval", &tmp),
                          sample->humidity = tmp);
    COUNT_OPTIONAL(parse_int_field(payload, "alcohol_raw", &sample->alcohol_raw));
    COUNT_OPTIONAL(parse_int_field(payload, "alcohol", &sample->alcohol_raw));
    COUNT_OPTIONAL(parse_property_int_field(payload, "aclval", &sample->alcohol_raw));
    COUNT_OPTIONAL(parse_int_field(payload, "alcohol_level", &sample->alcohol_level));
    COUNT_OPTIONAL(parse_bool_field(payload, "alcohol_alarm", &sample->alcohol_alarm));
    COUNT_OPTIONAL(parse_int_field(payload, "accel_x", &sample->accel_x));
    COUNT_OPTIONAL(parse_property_int_field(payload, "accx", &sample->accel_x));
    COUNT_OPTIONAL(parse_int_field(payload, "accel_y", &sample->accel_y));
    COUNT_OPTIONAL(parse_property_int_field(payload, "accy", &sample->accel_y));
    COUNT_OPTIONAL(parse_int_field(payload, "accel_z", &sample->accel_z));
    COUNT_OPTIONAL(parse_property_int_field(payload, "accz", &sample->accel_z));
    COUNT_OPTIONAL(parse_int_field(payload, "motion_state", &sample->motion_state));
    COUNT_OPTIONAL(parse_property_int_field(payload, "motionst", &sample->motion_state));
    COUNT_OPTIONAL(parse_bool_field(payload, "motion_alarm", &sample->motion_alarm));
    COUNT_OPTIONAL(parse_int_field(payload, "flame_status", &sample->flame_status));
    COUNT_OPTIONAL(parse_property_int_field(payload, "flamest", &sample->flame_status));
    COUNT_OPTIONAL(parse_bool_field(payload, "flame_valid", &sample->flame_valid));
    COUNT_OPTIONAL(parse_int_field(payload, "dht11_temperature", &sample->dht11_temperature));
    COUNT_OPTIONAL(parse_property_int_field(payload, "dht11temp", &sample->dht11_temperature));
    COUNT_OPTIONAL(parse_int_field(payload, "dht11_humidity", &sample->dht11_humidity));
    COUNT_OPTIONAL(parse_property_int_field(payload, "dht11hum", &sample->dht11_humidity));
    COUNT_OPTIONAL(parse_bool_field(payload, "dht11_valid", &sample->dht11_valid));
    COUNT_OPTIONAL(parse_double_field(payload, "gps_lat", &sample->gps_lat));
    COUNT_OPTIONAL(parse_double_field(payload, "lat", &sample->gps_lat));
    COUNT_OPTIONAL_ASSIGN(parse_property_double_field(payload, "gpslat", &tmp),
                          sample->gps_lat = tmp);
    COUNT_OPTIONAL(parse_double_field(payload, "gps_lon", &sample->gps_lon));
    COUNT_OPTIONAL(parse_double_field(payload, "lon", &sample->gps_lon));
    COUNT_OPTIONAL_ASSIGN(parse_property_double_field(payload, "gpslon", &tmp),
                          sample->gps_lon = tmp);
    COUNT_OPTIONAL_FLOAT(parse_double_field(payload, "gps_speed", &gps_speed), gps_speed);
    COUNT_OPTIONAL_FLOAT(parse_property_double_field(payload, "gpsspd", &tmp), tmp);
    COUNT_OPTIONAL(parse_int_field(payload, "gps_satellites", &sample->gps_satellites));
    COUNT_OPTIONAL(parse_property_int_field(payload, "gpssat", &sample->gps_satellites));
    COUNT_OPTIONAL(parse_bool_field(payload, "gps_fence_alarm", &sample->gps_fence_alarm));
    COUNT_OPTIONAL(parse_property_bool_field(payload, "gpsfence", &sample->gps_fence_alarm));

#undef COUNT_OPTIONAL_FLOAT
#undef COUNT_OPTIONAL_ASSIGN
#undef COUNT_OPTIONAL
#undef CHECK_OPTIONAL

    if (parsed_fields == 0) {
        return -1;
    }

    if (sample->dht11_temperature != 0 || sample->dht11_humidity != 0) {
        sample->dht11_valid = 1;
    }
    if (find_key(payload, "flamest") != NULL || find_key(payload, "flame_status") != NULL) {
        sample->flame_valid = 1;
    }

    return 0;
}
