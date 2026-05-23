#include "device/beep_control.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "config/app_config.h"

typedef struct beep_control_ctx {
    int fd;
    int current_state;
} beep_control_ctx_t;

static int beep_control_reopen(beep_control_ctx_t *beep_ctx)
{
    beep_ctx->fd = open(BEEP_DEVICE_PATH, O_RDWR);
    if (beep_ctx->fd == -1) {
        perror("open beep device failed");
        return -1;
    }

    return 0;
}

int beep_control_init(void **ctx)
{
    beep_control_ctx_t *beep_ctx = NULL;
    int ret = 0;

    if (ctx == NULL) {
        return -1;
    }

    beep_ctx = (beep_control_ctx_t *)malloc(sizeof(*beep_ctx));
    if (beep_ctx == NULL) {
        perror("malloc beep ctx failed");
        return -1;
    }

    beep_ctx->fd = -1;
    beep_ctx->current_state = BEEP_OFF;
    *ctx = beep_ctx;
    ret = beep_control_reopen(beep_ctx);
    return ret;
}

int beep_control_set(void *ctx, int state)
{
    beep_control_ctx_t *beep_ctx = (beep_control_ctx_t *)ctx;
    ssize_t write_size = 0;

    if (beep_ctx == NULL) {
        return -1;
    }

    if (state != BEEP_OFF && state != BEEP_ON) {
        return -1;
    }

    if (beep_ctx->current_state == state) {
        return 0;
    }

    if (beep_ctx->fd == -1 && beep_control_reopen(beep_ctx) != 0) {
        return -1;
    }

    write_size = write(beep_ctx->fd, &state, sizeof(state));
    if (write_size < 0) {
        perror("write beep state failed");
        close(beep_ctx->fd);
        beep_ctx->fd = -1;
        return -1;
    }

    beep_ctx->current_state = state;
    return 0;
}

void beep_control_deinit(void *ctx)
{
    beep_control_ctx_t *beep_ctx = (beep_control_ctx_t *)ctx;

    if (beep_ctx == NULL) {
        return;
    }

    if (beep_ctx->fd != -1) {
        close(beep_ctx->fd);
    }

    free(beep_ctx);
}
