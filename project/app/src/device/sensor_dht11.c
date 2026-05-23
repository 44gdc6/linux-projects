#include "device/sensor_dht11.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "config/app_config.h"
#include "core/log.h"

typedef struct dht11_sensor_ctx {
    int fd;
} dht11_sensor_ctx_t;

static int dht11_sensor_reopen(dht11_sensor_ctx_t *sensor_ctx)
{
    sensor_ctx->fd = open(DHT11_DEVICE_PATH, O_RDONLY);
    if (sensor_ctx->fd == -1) {
        perror("open dht11 device failed");
        return -1;
    }

    return 0;
}

int dht11_sensor_init(void **ctx)
{
    dht11_sensor_ctx_t *sensor_ctx = NULL;
    int ret = 0;

    if (ctx == NULL) {
        return -1;
    }

    sensor_ctx = (dht11_sensor_ctx_t *)malloc(sizeof(*sensor_ctx));
    if (sensor_ctx == NULL) {
        perror("malloc dht11 sensor ctx failed");
        return -1;
    }

    sensor_ctx->fd = -1;
    *ctx = sensor_ctx;
    ret = dht11_sensor_reopen(sensor_ctx);
    return ret;
}

int dht11_sensor_read(void *ctx, data_t *sample)
{
    dht11_sensor_ctx_t *sensor_ctx = (dht11_sensor_ctx_t *)ctx;
    unsigned char data[5] = {0};
    ssize_t read_size = 0;

    if (sensor_ctx == NULL || sample == NULL) {
        return -1;
    }

    if (sensor_ctx->fd == -1 && dht11_sensor_reopen(sensor_ctx) != 0) {
        return -1;
    }

    read_size = read(sensor_ctx->fd, data, sizeof(data));
    if (read_size < 0) {
        perror("read dht11 data failed");
        LOG_ERROR("dht11 read failed: fd=%d errno path=%s", sensor_ctx->fd, DHT11_DEVICE_PATH);
        close(sensor_ctx->fd);
        sensor_ctx->fd = -1;
        return -1;
    }

    if (read_size != (ssize_t)sizeof(data)) {
        fprintf(stderr, "unexpected dht11 read size: %ld\n", (long)read_size);
        LOG_WARN("unexpected dht11 read size: %ld", (long)read_size);
        return -1;
    }

    LOG_INFO("dht11 raw bytes: %02x %02x %02x %02x %02x",
             data[0], data[1], data[2], data[3], data[4]);

    sample->hum = data[0] + (data[1] / 10.0);
    sample->hum_valid = 1;
    LOG_INFO("dht11 humidity parsed: %.2f", sample->hum);
    return 0;
}

void dht11_sensor_deinit(void *ctx)
{
    dht11_sensor_ctx_t *sensor_ctx = (dht11_sensor_ctx_t *)ctx;

    if (sensor_ctx == NULL) {
        return;
    }

    if (sensor_ctx->fd != -1) {
        close(sensor_ctx->fd);
    }

    free(sensor_ctx);
}
