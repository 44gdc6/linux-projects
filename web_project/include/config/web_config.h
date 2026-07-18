#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#include <stddef.h>

typedef struct web_config {
    int collect_interval;
    char mqtt_mode[32];
    int alcohol_high;
    int motion_delta;
} web_config_t;

void web_config_defaults(web_config_t *config);
int web_config_load(web_config_t *config, const char *path);
int web_config_save(const web_config_t *config, const char *path);
int web_config_apply_json(web_config_t *config, const char *json);
int web_config_to_json(const web_config_t *config, char *buffer, size_t buffer_size);

#endif
