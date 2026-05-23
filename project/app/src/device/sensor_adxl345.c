#include "device/sensor_adxl345.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "config/app_config.h"

typedef struct adxl345_sensor_ctx {
    int fd;
} adxl345_sensor_ctx_t;

static int adxl345_sensor_reopen(adxl345_sensor_ctx_t *sensor_ctx)
{
    sensor_ctx->fd = open(ADXL345_DEVICE_PATH, O_RDONLY);
    if (sensor_ctx->fd == -1) {
        perror("open adxl345 device failed");
        return -1;
    }

    return 0;
}

int adxl345_sensor_init(void **ctx)
{
    adxl345_sensor_ctx_t *sensor_ctx = NULL;
    int ret = 0;

    if (ctx == NULL) {
        return -1;
    }

    sensor_ctx = (adxl345_sensor_ctx_t *)malloc(sizeof(*sensor_ctx));
    if (sensor_ctx == NULL) {
        perror("malloc adxl345 sensor ctx failed");
        return -1;
    }

    sensor_ctx->fd = -1;
    *ctx = sensor_ctx;
    ret = adxl345_sensor_reopen(sensor_ctx);
    return ret;
}

int adxl345_sensor_read(void *ctx, data_t *sample)
{
    adxl345_sensor_ctx_t *sensor_ctx = (adxl345_sensor_ctx_t *)ctx;
    short accel_raw[3] = {0};
    ssize_t read_size = 0;

    if (sensor_ctx == NULL || sample == NULL) {
        return -1;
    }

    if (sensor_ctx->fd == -1 && adxl345_sensor_reopen(sensor_ctx) != 0) {
        return -1;
    }

    read_size = read(sensor_ctx->fd, accel_raw, sizeof(accel_raw));
    if (read_size < 0) {
        perror("read adxl345 value failed");
        close(sensor_ctx->fd);
        sensor_ctx->fd = -1;
        return -1;
    }

    if (read_size != (ssize_t)sizeof(accel_raw)) {
        fprintf(stderr, "unexpected adxl345 read size: %ld\n", (long)read_size);
        return -1;
    }

    sample->accel_x = accel_raw[0];
    sample->accel_y = accel_raw[1];
    sample->accel_z = accel_raw[2];
    sample->accel_valid = 1;
    return 0;
}

void adxl345_sensor_deinit(void *ctx)
{
    adxl345_sensor_ctx_t *sensor_ctx = (adxl345_sensor_ctx_t *)ctx;

    if (sensor_ctx == NULL) {
        return;
    }

    if (sensor_ctx->fd != -1) {
        close(sensor_ctx->fd);
    }

    free(sensor_ctx);
}
