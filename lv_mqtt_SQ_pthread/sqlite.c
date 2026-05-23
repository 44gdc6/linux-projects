#include "sqlite.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app_config.h"
#include "linkqueue.h"
#include "mailbox.h"
#include "sensor_common.h"
#include "sqlite3.h"

typedef struct sqlite_runtime {
    sqlite3 *db;
    time_t last_open_attempt;
} sqlite_runtime_t;

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
    static const char *create_table_sql =
        "CREATE TABLE IF NOT EXISTS temperature_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "created_at TEXT NOT NULL,"
        "temp REAL NOT NULL,"
        "source TEXT NOT NULL"
        ");";
    char *errmsg = NULL;

    if (sqlite3_open(SQLITE_DB_PATH, &runtime->db) != SQLITE_OK) {
        printf("sqlite open failed: %s\n", sqlite3_errmsg(runtime->db));
        sqlite3_close(runtime->db);
        runtime->db = NULL;
        return -1;
    }

    if (sqlite3_exec(runtime->db, create_table_sql, NULL, NULL, &errmsg) != SQLITE_OK) {
        printf("sqlite create table failed: %s\n", errmsg == NULL ? "unknown" : errmsg);
        sqlite3_free(errmsg);
        sqlite3_close(runtime->db);
        runtime->db = NULL;
        return -1;
    }

    return 0;
}

static int sqlite_insert_temperature(sqlite_runtime_t *runtime, const data_t *sample)
{
    static const char *insert_sql =
        "INSERT INTO temperature_history(created_at, temp, source) VALUES(?, ?, ?);";
    sqlite3_stmt *stmt = NULL;
    char time_buffer[32];

    if (runtime->db == NULL) {
        return -1;
    }

    format_sample_time(sample->sample_time, time_buffer, sizeof(time_buffer));

    if (sqlite3_prepare_v2(runtime->db, insert_sql, -1, &stmt, NULL) != SQLITE_OK) {
        printf("sqlite prepare failed: %s\n", sqlite3_errmsg(runtime->db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, time_buffer, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, sample->temp);
    sqlite3_bind_text(stmt, 3, temp_source_name(sample->temp_source), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        printf("sqlite insert failed: %s\n", sqlite3_errmsg(runtime->db));
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
                sqlite_prepare_db(&runtime);
            }
        }

        if (mailbox_recv_msg(&sample) <= 0) {
            usleep(200000);
            continue;
        }

        if (!sample.temp_valid || runtime.db == NULL) {
            continue;
        }

        if (sqlite_insert_temperature(&runtime, &sample) != 0) {
            sqlite3_close(runtime.db);
            runtime.db = NULL;
        }
    }

    return NULL;
}
