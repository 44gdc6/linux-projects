#ifndef STORAGE_H
#define STORAGE_H

#include "ingest/payload_parser.h"

#include <stddef.h>
#include <stdint.h>

typedef struct storage storage_t;

int storage_open(storage_t **out, const char *db_path);
void storage_close(storage_t *storage);
int storage_insert_sample(storage_t *storage, const device_sample_t *sample);
int storage_history_range_json(storage_t *storage, const char *sensor,
                               uint64_t start_time, uint64_t end_time,
                               int limit, char *buffer, size_t buffer_size);
int storage_history_json(storage_t *storage, const char *sensor, int limit, char *buffer, size_t buffer_size);
int storage_alarms_json(storage_t *storage, int limit, char *buffer, size_t buffer_size);
int storage_export_csv(storage_t *storage, const char *sensor,
                       uint64_t start_time, uint64_t end_time,
                       int limit, char *buffer, size_t buffer_size);

#endif
