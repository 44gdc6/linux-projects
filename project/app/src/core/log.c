#include "core/log.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

static FILE *g_log_file = NULL;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static log_level_t g_log_level = LOG_LEVEL_INFO;
static int g_log_lock_ready = 0;

static const char *log_level_name(log_level_t level)
{
    switch (level) {
    case LOG_LEVEL_FATAL:
        return "FATAL";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    case LOG_LEVEL_WARN:
        return "WARN";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    case LOG_LEVEL_TRACE:
        return "TRACE";
    default:
        return "UNKNOWN";
    }
}

int log_init(const char *path)
{
    if (path == NULL) {
        return -1;
    }

    if (!g_log_lock_ready) {
        if (pthread_mutex_init(&g_log_lock, NULL) != 0) {
            return -1;
        }
        g_log_lock_ready = 1;
    }

    pthread_mutex_lock(&g_log_lock);
    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }

    g_log_file = fopen(path, "a");
    pthread_mutex_unlock(&g_log_lock);

    if (g_log_file == NULL) {
        perror("log_init fopen failed");
        return -1;
    }

    return 0;
}

int log_deinit(void)
{
    if (!g_log_lock_ready) {
        return 0;
    }

    pthread_mutex_lock(&g_log_lock);
    if (g_log_file != NULL) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    pthread_mutex_unlock(&g_log_lock);

    pthread_mutex_destroy(&g_log_lock);
    g_log_lock_ready = 0;
    return 0;
}

void log_set_level(log_level_t level)
{
    g_log_level = level;
}

int log_write(log_level_t level, const char *file, int line, const char *fmt, ...)
{
    time_t now;
    struct tm tm_info;
    va_list args;

    if (!g_log_lock_ready || g_log_file == NULL || fmt == NULL || level > g_log_level) {
        return 0;
    }

    time(&now);
#ifdef _WIN32
    localtime_s(&tm_info, &now);
#else
    localtime_r(&now, &tm_info);
#endif

    pthread_mutex_lock(&g_log_lock);
    fprintf(g_log_file,
            "[%04d-%02d-%02d %02d:%02d:%02d][%s][%s:%d] ",
            tm_info.tm_year + 1900,
            tm_info.tm_mon + 1,
            tm_info.tm_mday,
            tm_info.tm_hour,
            tm_info.tm_min,
            tm_info.tm_sec,
            log_level_name(level),
            file == NULL ? "unknown" : file,
            line);

    va_start(args, fmt);
    vfprintf(g_log_file, fmt, args);
    va_end(args);
    fputc('\n', g_log_file);
    fflush(g_log_file);
    pthread_mutex_unlock(&g_log_lock);

    return 0;
}
