#ifndef REALTIME_CACHE_H
#define REALTIME_CACHE_H

#include "ingest/payload_parser.h"

#include <stddef.h>
#include <pthread.h>

typedef struct realtime_cache {
    pthread_mutex_t lock;
    device_sample_t sample;
    int has_sample;
} realtime_cache_t;

int realtime_cache_init(realtime_cache_t *cache);
void realtime_cache_destroy(realtime_cache_t *cache);
void realtime_cache_update(realtime_cache_t *cache, const device_sample_t *sample);
int realtime_cache_copy(realtime_cache_t *cache, device_sample_t *out);
int realtime_cache_to_json(realtime_cache_t *cache, char *buffer, size_t buffer_size);

#endif
