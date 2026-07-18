#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "config/web_config.h"
#include "ingest/realtime_cache.h"
#include "storage/storage.h"

typedef struct app_context {
    const char *www_root;
    const char *db_path;
    const char *config_path;
    const char *auth_token;
    const char *cors_origin;
    const char *bind_host;
    int port;
    web_config_t config;
    realtime_cache_t *cache;
    storage_t *storage;
} app_context_t;

int http_server_run(app_context_t *ctx);

#endif
