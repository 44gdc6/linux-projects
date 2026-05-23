#include <stdio.h>
#include <pthread.h>
#include "linkqueue.h"
#include "mailbox.h"
#include "collect.h"
#include "mqtt.h"
#include "sqlite.h"

extern void *lvgl_thread(void *arg);


int main(void)
{
    mailbox_init();
    mailbox_add_task("collect_thread", collect_thread);
    mailbox_add_task("lvgl_thread", lvgl_thread);
    mailbox_add_task("sqlite_thread", sqlite_thread);
    mailbox_add_task("mqtt_thread", mqtt_thread);

    mailbox_waitall_thread_end();

    mailbox_destroy();

    return 0;
}
