#include "mq3_app.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MQ3_DEVICE_PATH "/dev/mq3_misc"

struct mq3_context {
    int fd;
};

mq3_context_t *mq3_init(void)
{
    mq3_context_t *ctx = (mq3_context_t *)malloc(sizeof(*ctx));
    if (ctx == NULL) {
        perror("malloc mq3 context failed");
        return NULL;
    }

    ctx->fd = open(MQ3_DEVICE_PATH, O_RDONLY);
    if (ctx->fd == -1) {
        perror("open mq3 device failed");
        free(ctx);
        return NULL;
    }

    printf("mq3 device opened: %s\n", MQ3_DEVICE_PATH);
    return ctx;
}

int mq3_read_raw(mq3_context_t *ctx)
{
    int raw_value = 0;
    ssize_t n;

    if (ctx == NULL || ctx->fd == -1) {
        return -1;
    }

    n = read(ctx->fd, &raw_value, sizeof(raw_value));
    if (n != sizeof(raw_value)) {
        if (n < 0) {
            perror("read mq3 failed");
        } else {
            fprintf(stderr, "mq3 short read: %ld bytes\n", (long)n);
        }
        return -1;
    }

    return raw_value;
}

void mq3_deinit(mq3_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->fd != -1) {
        close(ctx->fd);
    }
    free(ctx);
    printf("mq3 device closed\n");
}
