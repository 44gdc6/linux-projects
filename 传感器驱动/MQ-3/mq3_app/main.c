#include "mq3_app.h"
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    mq3_context_t *ctx = mq3_init();
    if (ctx == NULL) {
        fprintf(stderr, "mq3 init failed\n");
        return 1;
    }

    while (1) {
        int raw = mq3_read_raw(ctx);
        if (raw < 0) {
            fprintf(stderr, "mq3 read failed, exiting\n");
            break;
        }
        printf("MQ-3 ADC raw: %d\n", raw);
        sleep(1);
    }

    mq3_deinit(ctx);
    return 0;
}
