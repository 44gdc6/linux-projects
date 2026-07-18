#include "storage/storage.h"

#include <math.h>
#include <sqlite3.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORAGE_DB_BUSY_TIMEOUT_MS 3000
#define HISTORY_DEFAULT_LIMIT 100
#define HISTORY_MAX_LIMIT 1000
#define ALARM_DEFAULT_LIMIT 50
#define ALARM_MAX_LIMIT 500
#define EXPORT_DEFAULT_LIMIT 1000
#define EXPORT_MAX_LIMIT 10000
#define SENSOR_NAME_MAX 128
#define SQLITE_INT64_MAX_U64 9223372036854775807ULL

struct storage {
    sqlite3 *db;
};

static int append_format(char *buffer, size_t buffer_size, size_t *len,
                         const char *fmt, ...)
{
    va_list args;
    int written;

    if (buffer == NULL || buffer_size == 0 || len == NULL || *len >= buffer_size) {
        return -1;
    }

    va_start(args, fmt);
    written = vsnprintf(buffer + *len, buffer_size - *len, fmt, args);
    va_end(args);

    if (written < 0 || (size_t)written >= buffer_size - *len) {
        buffer[buffer_size - 1] = '\0';
        return -1;
    }

    *len += (size_t)written;
    return 0;
}

static int append_json_string(char *buffer, size_t buffer_size, size_t *len,
                              const char *input)
{
    if (input == NULL) {
        input = "";
    }

    if (append_format(buffer, buffer_size, len, "\"") != 0) {
        return -1;
    }

    for (; *input != '\0'; input++) {
        unsigned char ch = (unsigned char)*input;
        const char *escaped = NULL;

        switch (ch) {
        case '"': escaped = "\\\""; break;
        case '\\': escaped = "\\\\"; break;
        case '/': escaped = "\\/"; break;
        case '\b': escaped = "\\b"; break;
        case '\f': escaped = "\\f"; break;
        case '\n': escaped = "\\n"; break;
        case '\r': escaped = "\\r"; break;
        case '\t': escaped = "\\t"; break;
        default:
            break;
        }

        if (escaped != NULL) {
            if (append_format(buffer, buffer_size, len, "%s", escaped) != 0) {
                return -1;
            }
        } else if (ch < 0x20) {
            if (append_format(buffer, buffer_size, len, "\\u%04x", ch) != 0) {
                return -1;
            }
        } else if (append_format(buffer, buffer_size, len, "%c", ch) != 0) {
            return -1;
        }
    }

    return append_format(buffer, buffer_size, len, "\"");
}

static int append_json_double(char *buffer, size_t buffer_size, size_t *len,
                              double value)
{
    if (!isfinite(value)) {
        return append_format(buffer, buffer_size, len, "null");
    }
    return append_format(buffer, buffer_size, len, "%.2f", value);
}

static int append_csv_text(char *buffer, size_t buffer_size, size_t *len,
                           const char *input)
{
    int needs_quote = 0;
    int formula_prefix = 0;
    const char *p;

    if (input == NULL) {
        input = "";
    }
    if (*input == '=' || *input == '+' || *input == '-' || *input == '@') {
        needs_quote = 1;
        formula_prefix = 1;
    }
    for (p = input; *p != '\0'; p++) {
        if (*p == ',' || *p == '"' || *p == '\r' || *p == '\n') {
            needs_quote = 1;
            break;
        }
    }

    if (!needs_quote) {
        return append_format(buffer, buffer_size, len, "%s", input);
    }

    if (append_format(buffer, buffer_size, len, "\"%s", formula_prefix ? "'" : "") != 0) {
        return -1;
    }
    for (p = input; *p != '\0'; p++) {
        if (*p == '"') {
            if (append_format(buffer, buffer_size, len, "\"\"") != 0) {
                return -1;
            }
        } else {
            if (append_format(buffer, buffer_size, len, "%c", *p) != 0) {
                return -1;
            }
        }
    }
    return append_format(buffer, buffer_size, len, "\"");
}

static int append_csv_double(char *buffer, size_t buffer_size, size_t *len,
                             double value)
{
    if (!isfinite(value)) {
        return 0;
    }
    return append_format(buffer, buffer_size, len, "%.6g", value);
}

static int normalize_limit(int limit, int default_limit, int max_limit)
{
    if (limit <= 0) {
        return default_limit;
    }
    if (limit > max_limit) {
        return max_limit;
    }
    return limit;
}

static int validate_time_range(uint64_t start_time, uint64_t end_time,
                               int *has_start, int *has_end)
{
    if (has_start == NULL || has_end == NULL) {
        return -1;
    }

    *has_start = start_time != 0;
    *has_end = end_time != 0;

    if ((*has_start && start_time > SQLITE_INT64_MAX_U64) ||
        (*has_end && end_time > SQLITE_INT64_MAX_U64)) {
        return -1;
    }
    if (*has_start && *has_end && start_time > end_time) {
        return -1;
    }
    return 0;
}

static int validate_sensor_name(const char *sensor)
{
    size_t len;

    if (sensor == NULL || sensor[0] == '\0') {
        return -1;
    }

    len = strlen(sensor);
    if (len == 0 || len > SENSOR_NAME_MAX) {
        return -1;
    }
    return 0;
}

static int exec_sql(sqlite3 *db, const char *sql)
{
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "sqlite exec failed (%d): %s\n", rc, errmsg ? errmsg : "unknown");
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

static int prepare_stmt(sqlite3 *db, const char *sql, sqlite3_stmt **stmt)
{
    int rc;

    if (db == NULL || sql == NULL || stmt == NULL) {
        return -1;
    }

    rc = sqlite3_prepare_v2(db, sql, -1, stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "sqlite prepare failed (%d): %s\n", rc, sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

static int step_done(sqlite3 *db, sqlite3_stmt *stmt, const char *operation)
{
    int rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "sqlite %s failed (%d): %s\n",
                operation ? operation : "step", rc, sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

static int prepare_schema(sqlite3 *db)
{
    const char *schema =
        "PRAGMA busy_timeout=3000;"
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "CREATE TABLE IF NOT EXISTS device_samples ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "device TEXT NOT NULL,"
        "sample_time INTEGER NOT NULL,"
        "temperature REAL,"
        "humidity REAL,"
        "alcohol_raw INTEGER,"
        "alcohol_level INTEGER,"
        "alcohol_alarm INTEGER,"
        "accel_x INTEGER,"
        "accel_y INTEGER,"
        "accel_z INTEGER,"
        "motion_state INTEGER,"
        "motion_alarm INTEGER,"
        "flame_status INTEGER,"
        "flame_valid INTEGER,"
        "dht11_temperature INTEGER,"
        "dht11_humidity INTEGER,"
        "dht11_valid INTEGER,"
        "gps_lat REAL,"
        "gps_lon REAL,"
        "gps_speed REAL,"
        "gps_satellites INTEGER,"
        "gps_fence_alarm INTEGER,"
        "raw_payload TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_device_samples_time "
        "ON device_samples(device, sample_time);"
        "CREATE TABLE IF NOT EXISTS sensor_samples_flat ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "device TEXT NOT NULL,"
        "sample_time INTEGER NOT NULL,"
        "sensor_name TEXT NOT NULL,"
        "value REAL NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_flat_sensor_time "
        "ON sensor_samples_flat(sensor_name, sample_time);"
        "CREATE INDEX IF NOT EXISTS idx_flat_time "
        "ON sensor_samples_flat(sample_time);";

    return exec_sql(db, schema);
}

int storage_open(storage_t **out, const char *db_path)
{
    storage_t *storage;

    if (out == NULL || db_path == NULL) {
        return -1;
    }

    storage = calloc(1, sizeof(*storage));
    if (storage == NULL) {
        return -1;
    }

    if (sqlite3_open(db_path, &storage->db) != SQLITE_OK) {
        fprintf(stderr, "sqlite open failed: %s\n", sqlite3_errmsg(storage->db));
        sqlite3_close(storage->db);
        free(storage);
        return -1;
    }
    sqlite3_busy_timeout(storage->db, STORAGE_DB_BUSY_TIMEOUT_MS);
    sqlite3_extended_result_codes(storage->db, 1);

    if (prepare_schema(storage->db) != 0) {
        storage_close(storage);
        return -1;
    }

    *out = storage;
    return 0;
}

void storage_close(storage_t *storage)
{
    if (storage != NULL) {
        sqlite3_close(storage->db);
        free(storage);
    }
}

static int insert_flat(sqlite3 *db, const char *device, unsigned long long ts,
                       const char *sensor, double value)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "INSERT INTO sensor_samples_flat(device, sample_time, sensor_name, value) "
        "VALUES (?, ?, ?, ?);";

    if (prepare_stmt(db, sql, &stmt) != 0) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, device, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)ts);
    sqlite3_bind_text(stmt, 3, sensor, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, value);

    if (step_done(db, stmt, "insert flat sample") != 0) {
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

typedef struct sensor_value {
    const char *name;
    double value;
} sensor_value_t;

int storage_insert_sample(storage_t *storage, const device_sample_t *s)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "INSERT INTO device_samples(device, sample_time, temperature, humidity, "
        "alcohol_raw, alcohol_level, alcohol_alarm, accel_x, accel_y, accel_z, "
        "motion_state, motion_alarm, flame_status, flame_valid, dht11_temperature, "
        "dht11_humidity, dht11_valid, gps_lat, gps_lon, gps_speed, gps_satellites, "
        "gps_fence_alarm, raw_payload) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    size_t i;
    const sensor_value_t values[] = {
        {"temperature", s ? s->temperature : 0.0},
        {"humidity", s ? s->humidity : 0.0},
        {"alcohol_raw", s ? (double)s->alcohol_raw : 0.0},
        {"alcohol_level", s ? (double)s->alcohol_level : 0.0},
        {"alcohol_alarm", s ? (double)s->alcohol_alarm : 0.0},
        {"accel_x", s ? (double)s->accel_x : 0.0},
        {"accel_y", s ? (double)s->accel_y : 0.0},
        {"accel_z", s ? (double)s->accel_z : 0.0},
        {"motion_state", s ? (double)s->motion_state : 0.0},
        {"motion_alarm", s ? (double)s->motion_alarm : 0.0},
        {"flame_status", s ? (double)s->flame_status : 0.0},
        {"flame_valid", s ? (double)s->flame_valid : 0.0},
        {"dht11_temperature", s ? (double)s->dht11_temperature : 0.0},
        {"dht11_humidity", s ? (double)s->dht11_humidity : 0.0},
        {"dht11_valid", s ? (double)s->dht11_valid : 0.0},
        {"gps_lat", s ? s->gps_lat : 0.0},
        {"gps_lon", s ? s->gps_lon : 0.0},
        {"gps_speed", s ? (double)s->gps_speed : 0.0},
        {"gps_satellites", s ? (double)s->gps_satellites : 0.0},
        {"gps_fence_alarm", s ? (double)s->gps_fence_alarm : 0.0}
    };

    if (storage == NULL || s == NULL || s->timestamp > SQLITE_INT64_MAX_U64) {
        return -1;
    }

    if (exec_sql(storage->db, "BEGIN IMMEDIATE;") != 0) {
        return -1;
    }

    if (prepare_stmt(storage->db, sql, &stmt) != 0) {
        goto rollback;
    }

    sqlite3_bind_text(stmt, 1, s->device, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)s->timestamp);
    sqlite3_bind_double(stmt, 3, s->temperature);
    sqlite3_bind_double(stmt, 4, s->humidity);
    sqlite3_bind_int(stmt, 5, s->alcohol_raw);
    sqlite3_bind_int(stmt, 6, s->alcohol_level);
    sqlite3_bind_int(stmt, 7, s->alcohol_alarm);
    sqlite3_bind_int(stmt, 8, s->accel_x);
    sqlite3_bind_int(stmt, 9, s->accel_y);
    sqlite3_bind_int(stmt, 10, s->accel_z);
    sqlite3_bind_int(stmt, 11, s->motion_state);
    sqlite3_bind_int(stmt, 12, s->motion_alarm);
    sqlite3_bind_int(stmt, 13, s->flame_status);
    sqlite3_bind_int(stmt, 14, s->flame_valid);
    sqlite3_bind_int(stmt, 15, s->dht11_temperature);
    sqlite3_bind_int(stmt, 16, s->dht11_humidity);
    sqlite3_bind_int(stmt, 17, s->dht11_valid);
    sqlite3_bind_double(stmt, 18, s->gps_lat);
    sqlite3_bind_double(stmt, 19, s->gps_lon);
    sqlite3_bind_double(stmt, 20, s->gps_speed);
    sqlite3_bind_int(stmt, 21, s->gps_satellites);
    sqlite3_bind_int(stmt, 22, s->gps_fence_alarm);
    sqlite3_bind_text(stmt, 23, s->raw_payload, -1, SQLITE_TRANSIENT);

    if (step_done(storage->db, stmt, "insert device sample") != 0) {
        goto rollback;
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        if (insert_flat(storage->db, s->device, s->timestamp,
                        values[i].name, values[i].value) != 0) {
            goto rollback;
        }
    }

    if (exec_sql(storage->db, "COMMIT;") != 0) {
        exec_sql(storage->db, "ROLLBACK;");
        return -1;
    }
    return 0;

rollback:
    if (stmt != NULL) {
        sqlite3_finalize(stmt);
    }
    exec_sql(storage->db, "ROLLBACK;");
    return -1;
}

static int build_flat_query(char *sql, size_t sql_size, const char *select_clause,
                            int has_sensor, int has_start, int has_end,
                            const char *order_clause, int with_limit)
{
    size_t len = 0;
    int needs_and = 0;

    if (append_format(sql, sql_size, &len, "%s FROM sensor_samples_flat", select_clause) != 0) {
        return -1;
    }

    if (has_sensor || has_start || has_end) {
        if (append_format(sql, sql_size, &len, " WHERE ") != 0) {
            return -1;
        }
    }
    if (has_sensor) {
        if (append_format(sql, sql_size, &len, "sensor_name = ?") != 0) {
            return -1;
        }
        needs_and = 1;
    }
    if (has_start) {
        if (append_format(sql, sql_size, &len, "%ssample_time >= ?",
                          needs_and ? " AND " : "") != 0) {
            return -1;
        }
        needs_and = 1;
    }
    if (has_end) {
        if (append_format(sql, sql_size, &len, "%ssample_time <= ?",
                          needs_and ? " AND " : "") != 0) {
            return -1;
        }
    }
    if (order_clause != NULL && order_clause[0] != '\0') {
        if (append_format(sql, sql_size, &len, " %s", order_clause) != 0) {
            return -1;
        }
    }
    if (with_limit) {
        if (append_format(sql, sql_size, &len, " LIMIT ?") != 0) {
            return -1;
        }
    }
    if (append_format(sql, sql_size, &len, ";") != 0) {
        return -1;
    }
    return 0;
}

static int bind_flat_filters(sqlite3_stmt *stmt, const char *sensor,
                             int has_sensor, uint64_t start_time,
                             uint64_t end_time, int has_start, int has_end,
                             int with_limit, int limit)
{
    int index = 1;

    if (has_sensor &&
        sqlite3_bind_text(stmt, index++, sensor, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        return -1;
    }
    if (has_start &&
        sqlite3_bind_int64(stmt, index++, (sqlite3_int64)start_time) != SQLITE_OK) {
        return -1;
    }
    if (has_end &&
        sqlite3_bind_int64(stmt, index++, (sqlite3_int64)end_time) != SQLITE_OK) {
        return -1;
    }
    if (with_limit && sqlite3_bind_int(stmt, index++, limit) != SQLITE_OK) {
        return -1;
    }
    return 0;
}

int storage_history_range_json(storage_t *storage, const char *sensor,
                               uint64_t start_time, uint64_t end_time,
                               int limit, char *buffer, size_t buffer_size)
{
    sqlite3_stmt *stmt = NULL;
    sqlite3_stmt *count_stmt = NULL;
    char sql[512];
    char count_sql[512];
    const char *sensor_name = (sensor != NULL && sensor[0] != '\0') ? sensor : "temperature";
    long long total = 0;
    size_t len = 0;
    int first = 1;
    int has_start = 0;
    int has_end = 0;
    int rc;

    if (storage == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }
    buffer[0] = '\0';

    if (validate_sensor_name(sensor_name) != 0 ||
        validate_time_range(start_time, end_time, &has_start, &has_end) != 0) {
        return -1;
    }
    limit = normalize_limit(limit, HISTORY_DEFAULT_LIMIT, HISTORY_MAX_LIMIT);

    if (build_flat_query(count_sql, sizeof(count_sql), "SELECT COUNT(*)",
                         1, has_start, has_end, NULL, 0) != 0 ||
        prepare_stmt(storage->db, count_sql, &count_stmt) != 0 ||
        bind_flat_filters(count_stmt, sensor_name, 1, start_time, end_time,
                          has_start, has_end, 0, 0) != 0) {
        goto fail;
    }

    rc = sqlite3_step(count_stmt);
    if (rc == SQLITE_ROW) {
        total = sqlite3_column_int64(count_stmt, 0);
    } else {
        fprintf(stderr, "sqlite count history failed (%d): %s\n", rc, sqlite3_errmsg(storage->db));
        goto fail;
    }
    sqlite3_finalize(count_stmt);
    count_stmt = NULL;

    if (build_flat_query(sql, sizeof(sql), "SELECT sample_time, value",
                         1, has_start, has_end,
                         "ORDER BY sample_time DESC", 1) != 0 ||
        prepare_stmt(storage->db, sql, &stmt) != 0 ||
        bind_flat_filters(stmt, sensor_name, 1, start_time, end_time,
                          has_start, has_end, 1, limit) != 0) {
        goto fail;
    }

    if (append_format(buffer, buffer_size, &len, "{\"sensor\":") != 0 ||
        append_json_string(buffer, buffer_size, &len, sensor_name) != 0 ||
        append_format(buffer, buffer_size, &len, ",\"limit\":%d", limit) != 0) {
        goto fail;
    }
    if (has_start &&
        append_format(buffer, buffer_size, &len, ",\"start\":%llu",
                      (unsigned long long)start_time) != 0) {
        goto fail;
    }
    if (has_end &&
        append_format(buffer, buffer_size, &len, ",\"end\":%llu",
                      (unsigned long long)end_time) != 0) {
        goto fail;
    }
    if (append_format(buffer, buffer_size, &len, ",\"total\":%lld,\"data\":[", total) != 0) {
        goto fail;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        long long ts = sqlite3_column_int64(stmt, 0);
        double value = sqlite3_column_double(stmt, 1);

        if (append_format(buffer, buffer_size, &len,
                          "%s{\"timestamp\":%lld,\"value\":",
                          first ? "" : ",", ts) != 0 ||
            append_json_double(buffer, buffer_size, &len, value) != 0 ||
            append_format(buffer, buffer_size, &len, "}") != 0) {
            goto fail;
        }
        first = 0;
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "sqlite read history failed (%d): %s\n", rc, sqlite3_errmsg(storage->db));
        goto fail;
    }
    if (append_format(buffer, buffer_size, &len, "]}") != 0) {
        goto fail;
    }
    sqlite3_finalize(stmt);
    return 0;

fail:
    if (count_stmt != NULL) {
        sqlite3_finalize(count_stmt);
    }
    if (stmt != NULL) {
        sqlite3_finalize(stmt);
    }
    return -1;
}

int storage_history_json(storage_t *storage, const char *sensor, int limit, char *buffer, size_t buffer_size)
{
    if (sensor == NULL) {
        return -1;
    }
    return storage_history_range_json(storage, sensor, 0, 0, limit, buffer, buffer_size);
}

static int append_alarm_event(char *buffer, size_t buffer_size, size_t *len,
                              int *first, sqlite3_int64 sample_id, int code,
                              sqlite3_int64 ts, const char *device,
                              const char *type, const char *sensor,
                              const char *level, const char *message)
{
    sqlite3_int64 event_id = sample_id * 10 + code;

    if (append_format(buffer, buffer_size, len,
                      "%s{\"id\":%lld,\"timestamp\":%lld,\"device\":",
                      *first ? "" : ",", (long long)event_id, (long long)ts) != 0 ||
        append_json_string(buffer, buffer_size, len, device ? device : "device") != 0 ||
        append_format(buffer, buffer_size, len, ",\"type\":") != 0 ||
        append_json_string(buffer, buffer_size, len, type) != 0 ||
        append_format(buffer, buffer_size, len, ",\"level\":") != 0 ||
        append_json_string(buffer, buffer_size, len, level) != 0 ||
        append_format(buffer, buffer_size, len, ",\"sensor\":") != 0 ||
        append_json_string(buffer, buffer_size, len, sensor) != 0 ||
        append_format(buffer, buffer_size, len, ",\"message\":") != 0 ||
        append_json_string(buffer, buffer_size, len, message) != 0 ||
        append_format(buffer, buffer_size, len, ",\"resolved\":false}") != 0) {
        return -1;
    }

    *first = 0;
    return 0;
}

int storage_alarms_json(storage_t *storage, int limit, char *buffer, size_t buffer_size)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT id, sample_time, device, alcohol_raw, alcohol_level, alcohol_alarm, "
        "accel_x, accel_y, accel_z, motion_state, motion_alarm, "
        "gps_fence_alarm, flame_status "
        "FROM device_samples WHERE alcohol_alarm != 0 OR motion_alarm != 0 "
        "OR gps_fence_alarm != 0 OR flame_status != 0 ORDER BY sample_time DESC LIMIT ?;";
    size_t len = 0;
    int first = 1;
    int emitted = 0;
    int rc;

    if (storage == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }
    buffer[0] = '\0';
    limit = normalize_limit(limit, ALARM_DEFAULT_LIMIT, ALARM_MAX_LIMIT);

    if (prepare_stmt(storage->db, sql, &stmt) != 0) {
        return -1;
    }
    sqlite3_bind_int(stmt, 1, limit);

    if (append_format(buffer, buffer_size, &len, "{\"alarms\":[") != 0) {
        sqlite3_finalize(stmt);
        return -1;
    }

    while (emitted < limit && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        sqlite3_int64 sample_id = sqlite3_column_int64(stmt, 0);
        sqlite3_int64 ts = sqlite3_column_int64(stmt, 1);
        const char *device = (const char *)sqlite3_column_text(stmt, 2);
        int alcohol_raw = sqlite3_column_int(stmt, 3);
        int alcohol_level = sqlite3_column_int(stmt, 4);
        int alcohol_alarm = sqlite3_column_int(stmt, 5);
        int accel_x = sqlite3_column_int(stmt, 6);
        int accel_y = sqlite3_column_int(stmt, 7);
        int accel_z = sqlite3_column_int(stmt, 8);
        int motion_state = sqlite3_column_int(stmt, 9);
        int motion_alarm = sqlite3_column_int(stmt, 10);
        int fence_alarm = sqlite3_column_int(stmt, 11);
        int flame = sqlite3_column_int(stmt, 12);
        char message[160];

        if (alcohol_alarm && emitted < limit) {
            snprintf(message, sizeof(message), "Alcohol alarm raw=%d level=%d",
                     alcohol_raw, alcohol_level);
            if (append_alarm_event(buffer, buffer_size, &len, &first,
                                   sample_id, 1, ts, device, "alcohol",
                                   "alcohol_raw",
                                   alcohol_level >= 2 ? "error" : "warning",
                                   message) != 0) {
                goto fail;
            }
            emitted++;
        }
        if (motion_alarm && emitted < limit) {
            snprintf(message, sizeof(message),
                     "Motion alarm state=%d accel=(%d,%d,%d)",
                     motion_state, accel_x, accel_y, accel_z);
            if (append_alarm_event(buffer, buffer_size, &len, &first,
                                   sample_id, 2, ts, device, "motion",
                                   "motion_state", "warning", message) != 0) {
                goto fail;
            }
            emitted++;
        }
        if (fence_alarm && emitted < limit) {
            snprintf(message, sizeof(message), "GPS fence alarm");
            if (append_alarm_event(buffer, buffer_size, &len, &first,
                                   sample_id, 3, ts, device, "geofence",
                                   "gps_fence_alarm", "warning", message) != 0) {
                goto fail;
            }
            emitted++;
        }
        if (flame && emitted < limit) {
            snprintf(message, sizeof(message), "Flame alarm status=%d", flame);
            if (append_alarm_event(buffer, buffer_size, &len, &first,
                                   sample_id, 4, ts, device, "flame",
                                   "flame_status", "error", message) != 0) {
                goto fail;
            }
            emitted++;
        }
    }
    if (emitted < limit && rc != SQLITE_DONE) {
        fprintf(stderr, "sqlite read alarms failed (%d): %s\n", rc, sqlite3_errmsg(storage->db));
        goto fail;
    }
    if (append_format(buffer, buffer_size, &len, "]}") != 0) {
        goto fail;
    }
    sqlite3_finalize(stmt);
    return 0;

fail:
    sqlite3_finalize(stmt);
    return -1;
}

int storage_export_csv(storage_t *storage, const char *sensor,
                       uint64_t start_time, uint64_t end_time,
                       int limit, char *buffer, size_t buffer_size)
{
    sqlite3_stmt *stmt = NULL;
    char sql[512];
    const char *sensor_filter = (sensor != NULL && sensor[0] != '\0') ? sensor : NULL;
    int has_sensor = sensor_filter != NULL;
    int has_start = 0;
    int has_end = 0;
    size_t len = 0;
    int rc;

    if (storage == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }
    buffer[0] = '\0';

    if ((has_sensor && validate_sensor_name(sensor_filter) != 0) ||
        validate_time_range(start_time, end_time, &has_start, &has_end) != 0) {
        return -1;
    }
    limit = normalize_limit(limit, EXPORT_DEFAULT_LIMIT, EXPORT_MAX_LIMIT);

    if (build_flat_query(sql, sizeof(sql), "SELECT sample_time, sensor_name, value",
                         has_sensor, has_start, has_end,
                         "ORDER BY sample_time ASC, sensor_name ASC", 1) != 0 ||
        prepare_stmt(storage->db, sql, &stmt) != 0 ||
        bind_flat_filters(stmt, sensor_filter, has_sensor, start_time, end_time,
                          has_start, has_end, 1, limit) != 0) {
        goto fail;
    }

    if (append_format(buffer, buffer_size, &len, "timestamp,sensor,value\n") != 0) {
        goto fail;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        sqlite3_int64 ts = sqlite3_column_int64(stmt, 0);
        const char *sensor_name = (const char *)sqlite3_column_text(stmt, 1);
        double value = sqlite3_column_double(stmt, 2);

        if (append_format(buffer, buffer_size, &len, "%lld,", (long long)ts) != 0 ||
            append_csv_text(buffer, buffer_size, &len, sensor_name ? sensor_name : "") != 0 ||
            append_format(buffer, buffer_size, &len, ",") != 0 ||
            append_csv_double(buffer, buffer_size, &len, value) != 0 ||
            append_format(buffer, buffer_size, &len, "\n") != 0) {
            goto fail;
        }
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "sqlite export csv failed (%d): %s\n", rc, sqlite3_errmsg(storage->db));
        goto fail;
    }

    sqlite3_finalize(stmt);
    return 0;

fail:
    if (stmt != NULL) {
        sqlite3_finalize(stmt);
    }
    return -1;
}
