#include "storage/sqlite.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config/app_config.h"
#include "core/log.h"
#include "core/linkqueue.h"
#include "core/mailbox.h"
#include "device/sensor_common.h"
#include "sqlite3.h"

typedef struct sqlite_runtime {
    sqlite3 *db;
    time_t last_open_attempt;
} sqlite_runtime_t;

#define SQLITE_SAMPLE_TABLE "sensor_samples"
#define SQLITE_LEGACY_TABLE "temperature_history"

static int sqlite_exec_ddl(sqlite3 *db, const char *sql)
{
    char *errmsg = NULL;
    int rc = 0;

    rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        printf("sqlite ddl failed: %s\n", errmsg == NULL ? "unknown" : errmsg);
        LOG_ERROR("sqlite ddl failed: %s", errmsg == NULL ? "unknown" : errmsg);
        sqlite3_free(errmsg);
        return -1;
    }

    return 0;
}

static int sqlite_ensure_column(sqlite3 *db, const char *sql)
{
    char *errmsg = NULL;
    int rc = 0;

    rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc == SQLITE_OK) {
        return 0;
    }

    if (errmsg != NULL && strstr(errmsg, "duplicate column name") != NULL) {
        sqlite3_free(errmsg);
        return 0;
    }

    printf("sqlite alter table failed: %s\n", errmsg == NULL ? "unknown" : errmsg);
    LOG_ERROR("sqlite alter table failed: %s", errmsg == NULL ? "unknown" : errmsg);
    sqlite3_free(errmsg);
    return -1;
}

static int sqlite_table_exists(sqlite3 *db, const char *table_name)
{
    static const char *query_sql =
        "SELECT name FROM sqlite_master WHERE type='table' AND name=?;";
    sqlite3_stmt *stmt = NULL;
    int rc = 0;
    int exists = 0;

    if (db == NULL || table_name == NULL) {
        return 0;
    }

    rc = sqlite3_prepare_v2(db, query_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("sqlite table exists check prepare failed: %s", sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    exists = (rc == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

static int sqlite_rename_legacy_table(sqlite3 *db)
{
    char sql[128];

    if (db == NULL) {
        return -1;
    }

    snprintf(sql,
             sizeof(sql),
             "ALTER TABLE %s RENAME TO %s;",
             SQLITE_LEGACY_TABLE,
             SQLITE_SAMPLE_TABLE);

    if (sqlite_exec_ddl(db, sql) != 0) {
        LOG_ERROR("sqlite legacy table rename failed: %s -> %s",
                  SQLITE_LEGACY_TABLE,
                  SQLITE_SAMPLE_TABLE);
        return -1;
    }

    LOG_INFO("sqlite legacy table migrated: %s -> %s",
             SQLITE_LEGACY_TABLE,
             SQLITE_SAMPLE_TABLE);
    return 0;
}

static int sqlite_prepare_sample_table(sqlite3 *db)
{
    static const char *create_table_sql =
        "CREATE TABLE IF NOT EXISTS sensor_samples ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "created_at TEXT NOT NULL,"
        "temp REAL NOT NULL,"
        "humidity REAL,"
        "source TEXT NOT NULL,"
        "alcohol_raw INTEGER,"
        "alcohol_level INTEGER,"
        "alcohol_alarm INTEGER NOT NULL DEFAULT 0,"
        "accel_x INTEGER,"
        "accel_y INTEGER,"
        "accel_z INTEGER,"
        "motion_state INTEGER,"
        "motion_alarm INTEGER NOT NULL DEFAULT 0,"
        "weather_day TEXT,"
        "weather_week TEXT,"
        "weather_city TEXT,"
        "weather_text TEXT,"
        "weather_temp TEXT"
        ");";

    if (sqlite_exec_ddl(db, create_table_sql) != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN humidity REAL;") != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN alcohol_raw INTEGER;") != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN alcohol_level INTEGER;") != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN alcohol_alarm INTEGER NOT NULL DEFAULT 0;") != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN accel_x INTEGER;") != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN accel_y INTEGER;") != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN accel_z INTEGER;") != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN motion_state INTEGER;") != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN motion_alarm INTEGER NOT NULL DEFAULT 0;") != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN weather_day TEXT;") != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN weather_week TEXT;") != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN weather_city TEXT;") != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN weather_text TEXT;") != 0 ||
        sqlite_ensure_column(db, "ALTER TABLE sensor_samples ADD COLUMN weather_temp TEXT;") != 0) {
        return -1;
    }

    return 0;
}

static void format_sample_time(time_t sample_time, char *buffer, size_t buffer_size)
{
    struct tm tm_info;

#ifdef _WIN32
    localtime_s(&tm_info, &sample_time);
#else
    localtime_r(&sample_time, &tm_info);
#endif
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &tm_info);
}

static int sqlite_prepare_db(sqlite_runtime_t *runtime)
{
    int sample_exists = 0;
    int legacy_exists = 0;

    if (sqlite3_open(SQLITE_DB_PATH, &runtime->db) != SQLITE_OK) {
        printf("sqlite open failed: %s\n", sqlite3_errmsg(runtime->db));
        LOG_ERROR("sqlite open failed: %s", sqlite3_errmsg(runtime->db));
        sqlite3_close(runtime->db);
        runtime->db = NULL;
        return -1;
    }

    sample_exists = sqlite_table_exists(runtime->db, SQLITE_SAMPLE_TABLE);
    legacy_exists = sqlite_table_exists(runtime->db, SQLITE_LEGACY_TABLE);

    if (sample_exists && legacy_exists) {
        LOG_WARN("sqlite found both %s and %s, continue using %s",
                 SQLITE_SAMPLE_TABLE,
                 SQLITE_LEGACY_TABLE,
                 SQLITE_SAMPLE_TABLE);
    } else if (!sample_exists && legacy_exists) {
        if (sqlite_rename_legacy_table(runtime->db) != 0) {
            sqlite3_close(runtime->db);
            runtime->db = NULL;
            return -1;
        }
    }

    if (sqlite_prepare_sample_table(runtime->db) != 0) {
        sqlite3_close(runtime->db);
        runtime->db = NULL;
        return -1;
    }

    return 0;
}

static int sqlite_insert_sample(sqlite_runtime_t *runtime, const data_t *sample)
{
    static const char *insert_sql =
        "INSERT INTO sensor_samples(created_at, temp, humidity, source, alcohol_raw, alcohol_level, alcohol_alarm, "
        "accel_x, accel_y, accel_z, motion_state, motion_alarm, weather_day, weather_week, weather_city, weather_text, weather_temp) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt = NULL;
    char time_buffer[32];

    if (runtime->db == NULL) {
        return -1;
    }

    format_sample_time(sample->sample_time, time_buffer, sizeof(time_buffer));

    if (sqlite3_prepare_v2(runtime->db, insert_sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("sqlite prepare failed: %s\n", sqlite3_errmsg(runtime->db));
        LOG_ERROR("sqlite prepare failed: %s", sqlite3_errmsg(runtime->db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, time_buffer, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, sample->temp);
    if (sample->hum_valid) {
        sqlite3_bind_double(stmt, 3, sample->hum);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_text(stmt, 4, temp_source_name(sample->temp_source), -1, SQLITE_TRANSIENT);
    if (sample->alcohol_valid) {
        sqlite3_bind_int(stmt, 5, sample->alcohol_raw);
        sqlite3_bind_int(stmt, 6, sample->alcohol_level);
    } else {
        sqlite3_bind_null(stmt, 5);
        sqlite3_bind_null(stmt, 6);
    }
    sqlite3_bind_int(stmt, 7, sample->alcohol_alarm);
    if (sample->accel_valid) {
        sqlite3_bind_int(stmt, 8, sample->accel_x);
        sqlite3_bind_int(stmt, 9, sample->accel_y);
        sqlite3_bind_int(stmt, 10, sample->accel_z);
        sqlite3_bind_int(stmt, 11, sample->motion_state);
    } else {
        sqlite3_bind_null(stmt, 8);
        sqlite3_bind_null(stmt, 9);
        sqlite3_bind_null(stmt, 10);
        sqlite3_bind_null(stmt, 11);
    }
    sqlite3_bind_int(stmt, 12, sample->motion_alarm);
    if (sample->weather_valid) {
        sqlite3_bind_text(stmt, 13, sample->weather_day, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 14, sample->weather_week, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 15, sample->weather_city, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 16, sample->weather_text, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 17, sample->weather_temp, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 13);
        sqlite3_bind_null(stmt, 14);
        sqlite3_bind_null(stmt, 15);
        sqlite3_bind_null(stmt, 16);
        sqlite3_bind_null(stmt, 17);
    }

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        printf("sqlite insert failed: %s\n", sqlite3_errmsg(runtime->db));
        LOG_ERROR("sqlite insert failed: %s", sqlite3_errmsg(runtime->db));
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

void *sqlite_thread(void *arg)
{
    sqlite_runtime_t runtime;
    data_t sample;

    (void)arg;
    memset(&runtime, 0, sizeof(runtime));

    while (1) {
        if (runtime.db == NULL) {
            time_t now = time(NULL);
            if (now - runtime.last_open_attempt >= SQLITE_RETRY_INTERVAL_SEC) {
                runtime.last_open_attempt = now;
                if (sqlite_prepare_db(&runtime) == 0) {
                    LOG_INFO("sqlite database ready: %s", SQLITE_DB_PATH);
                }
            }
        }

        if (mailbox_recv_msg(&sample) <= 0) {
            usleep(200000);
            continue;
        }

        if (!sample.temp_valid || runtime.db == NULL) {
            continue;
        }

        if (sqlite_insert_sample(&runtime, &sample) != 0) {
            sqlite3_close(runtime.db);
            runtime.db = NULL;
        }
    }

    return NULL;
}
