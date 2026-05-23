#include "device/sensor_mq3.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "config/app_config.h"

typedef struct mq3_sensor_ctx {
    int fd;
} mq3_sensor_ctx_t;

static int mq3_sensor_reopen(mq3_sensor_ctx_t *sensor_ctx)
{
    sensor_ctx->fd = open(MQ3_DEVICE_PATH, O_RDONLY);
    if (sensor_ctx->fd == -1) {
        perror("open mq3 device failed");
        return -1;
    }

    return 0;
}

int mq3_sensor_init(void **ctx)
{
    mq3_sensor_ctx_t *sensor_ctx = NULL;
    int ret = 0;

    if (ctx == NULL) {
        return -1;
    }

    sensor_ctx = (mq3_sensor_ctx_t *)malloc(sizeof(*sensor_ctx));
    if (sensor_ctx == NULL) {
        perror("malloc mq3 sensor ctx failed");
        return -1;
    }

    sensor_ctx->fd = -1;
    *ctx = sensor_ctx;
    ret = mq3_sensor_reopen(sensor_ctx);
    return ret;
}

int mq3_sensor_read(void *ctx, data_t *sample)
{
    mq3_sensor_ctx_t *sensor_ctx = (mq3_sensor_ctx_t *)ctx;
    ssize_t read_size = 0;

    if (sensor_ctx == NULL || sample == NULL) {
        return -1;
    }

    if (sensor_ctx->fd == -1 && mq3_sensor_reopen(sensor_ctx) != 0) {
        return -1;
    }

    read_size = read(sensor_ctx->fd, &sample->alcohol_raw, sizeof(sample->alcohol_raw));
    if (read_size < 0) {
        perror("read mq3 raw value failed");
        close(sensor_ctx->fd);
        sensor_ctx->fd = -1;
        return -1;
    }

    if (read_size != (ssize_t)sizeof(sample->alcohol_raw)) {
        fprintf(stderr, "unexpected mq3 read size: %ld\n", (long)read_size);
        return -1;
    }

    sample->alcohol_valid = 1;
    return 0;
}

void mq3_sensor_deinit(void *ctx)
{
    mq3_sensor_ctx_t *sensor_ctx = (mq3_sensor_ctx_t *)ctx;

    if (sensor_ctx == NULL) {
        return;
    }

    if (sensor_ctx->fd != -1) {
        close(sensor_ctx->fd);
    }

    free(sensor_ctx);
}
