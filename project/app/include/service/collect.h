#ifndef __COLLECT_H__
#define __COLLECT_H__

#include "core/linkqueue.h"

typedef struct sensor_ops {
    const char *name;
    int is_worker;
    int (*init)(void **ctx);
    int (*read)(void *ctx, data_t *sample);
    void (*deinit)(void *ctx);
} sensor_ops_t;

typedef struct sensor_registration {
    const sensor_ops_t *ops;
    void *ctx;
    int enabled;
} sensor_registration_t;

extern void *collect_thread(void *arg);

#endif
