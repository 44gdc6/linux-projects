#ifndef __BEEP_CONTROL_H__
#define __BEEP_CONTROL_H__

enum {
    BEEP_OFF = 0,
    BEEP_ON = 1,
};

int beep_control_init(void **ctx);
int beep_control_set(void *ctx, int state);
void beep_control_deinit(void *ctx);

#endif
