#include <pthread.h>
#include <stdio.h>

#include "comm/can_node.h"
#include "comm/mqtt.h"
#include "config/app_config.h"
#include "core/log.h"
#include "core/mailbox.h"
#include "service/collect.h"
#include "service/weather.h"
#include "storage/sqlite.h"

extern void *lvgl_thread(void *arg);


int main(void)
{
    if (log_init(APP_LOG_PATH) != 0) {
        fprintf(stderr, "log_init failed: %s\n", APP_LOG_PATH);
    } else {
        log_set_level(LOG_LEVEL_INFO);
        LOG_INFO("application startup");
    }

    mailbox_init();
    mailbox_add_task("collect_thread", collect_thread);
    LOG_INFO("thread registered: %s", "collect_thread");
    mailbox_add_task("weather_thread", weather_thread);
    LOG_INFO("thread registered: %s", "weather_thread");
    mailbox_add_task("lvgl_thread", lvgl_thread);
    LOG_INFO("thread registered: %s", "lvgl_thread");
    mailbox_add_task("sqlite_thread", sqlite_thread);
    LOG_INFO("thread registered: %s", "sqlite_thread");
    mailbox_add_task("mqtt_thread", mqtt_thread);
    LOG_INFO("thread registered: %s", "mqtt_thread");
    mailbox_add_task("can_thread", can_thread);
    LOG_INFO("thread registered: %s", "can_thread");

    mailbox_waitall_thread_end();

    mailbox_destroy();
    LOG_INFO("application shutdown");
    log_deinit();

    return 0;
}
