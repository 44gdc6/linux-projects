#include "sensor_lm75.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "app_config.h"
#include "sensor_common.h"

typedef struct lm75_sensor_ctx {
    int fd;
} lm75_sensor_ctx_t;

static int lm75_sensor_init(void **ctx)
{
    lm75_sensor_ctx_t *sensor_ctx = NULL;

    sensor_ctx = (lm75_sensor_ctx_t *)malloc(sizeof(*sensor_ctx));
    if (sensor_ctx == NULL) {
        perror("malloc lm75 sensor ctx failed");
        return -1;
    }

    sensor_ctx->fd = open(LM75_DEVICE_PATH, O_RDWR);
    if (sensor_ctx->fd == -1) {
        perror("open lm75 device failed");
    }

    *ctx = sensor_ctx;
    return 0;
}

static int lm75_sensor_read(void *ctx, data_t *sample)
{
    lm75_sensor_ctx_t *sensor_ctx = (lm75_sensor_ctx_t *)ctx;
    short raw_value = 0;
    ssize_t read_size = 0;

    if (sensor_ctx == NULL || sample == NULL) {
        return -1;
    }

    if (sensor_ctx->fd == -1) {
        sensor_ctx->fd = open(LM75_DEVICE_PATH, O_RDWR);
        if (sensor_ctx->fd == -1) {
            return -1;
        }
    }

    read_size = read(sensor_ctx->fd, &raw_value, sizeof(raw_value));
    /*
     * The current LM75 misc driver copies the raw value to userspace but
     * incorrectly returns 0 instead of sizeof(raw_value). Accept 0 here so
     * this userspace collector stays compatible with that driver.
     */
    if (read_size < 0) {
        close(sensor_ctx->fd);
        sensor_ctx->fd = -1;
        return -1;
    }

    sample->temp = lm75_raw_to_celsius(raw_value);
    sample->temp_valid = 1;
    sample->temp_source = TEMP_SOURCE_LM75;

    return 0;
}

static void lm75_sensor_deinit(void *ctx)
{
    lm75_sensor_ctx_t *sensor_ctx = (lm75_sensor_ctx_t *)ctx;

    if (sensor_ctx == NULL) {
        return;
    }

    if (sensor_ctx->fd != -1) {
        close(sensor_ctx->fd);
    }

    free(sensor_ctx);
}

const sensor_ops_t lm75_sensor_ops = {
    .name = "lm75",
    .is_worker = 0,
    .init = lm75_sensor_init,
    .read = lm75_sensor_read,
    .deinit = lm75_sensor_deinit,
};
