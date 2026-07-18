#include "http/http_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *arg_value(int argc, char **argv, const char *name, const char *fallback)
{
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return fallback;
}

static int parse_port(const char *text)
{
    long value;
    char *end = NULL;

    if (text == NULL || *text == '\0') {
        return 8080;
    }
    value = strtol(text, &end, 10);
    if (text == end || *end != '\0' || value < 1 || value > 65535) {
        return -1;
    }
    return (int)value;
}

int main(int argc, char **argv)
{
    realtime_cache_t cache;
    storage_t *storage = NULL;
    app_context_t ctx;
    const char *www_root = arg_value(argc, argv, "-r", "./www");
    const char *db_path = arg_value(argc, argv, "--db", "./sensor_history.db");
    const char *config_path = arg_value(argc, argv, "--config", "./web_config.json");
    const char *auth_token = getenv("SENSOR_WEB_TOKEN");
    const char *cors_origin = getenv("SENSOR_WEB_CORS_ORIGIN");
    const char *bind_host = getenv("SENSOR_WEB_BIND");
    int port = parse_port(arg_value(argc, argv, "-p", "8080"));

    if (bind_host == NULL || bind_host[0] == '\0') {
        bind_host = "127.0.0.1";
    }

    if (port < 0) {
        fprintf(stderr, "invalid port, expected 1..65535\n");
        return 1;
    }

    if (realtime_cache_init(&cache) != 0) {
        fprintf(stderr, "realtime cache init failed\n");
        return 1;
    }
    if (storage_open(&storage, db_path) != 0) {
        fprintf(stderr, "storage open failed: %s\n", db_path);
        realtime_cache_destroy(&cache);
        return 1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.www_root = www_root;
    ctx.db_path = db_path;
    ctx.config_path = config_path;
    ctx.auth_token = auth_token;
    ctx.cors_origin = cors_origin;
    ctx.bind_host = bind_host;
    ctx.port = port;
    ctx.cache = &cache;
    ctx.storage = storage;
    if (web_config_load(&ctx.config, config_path) != 0) {
        fprintf(stderr, "config load failed: %s\n", config_path);
        storage_close(storage);
        realtime_cache_destroy(&cache);
        return 1;
    }

    int rc = http_server_run(&ctx);
    storage_close(storage);
    realtime_cache_destroy(&cache);
    return rc == 0 ? 0 : 1;
}
