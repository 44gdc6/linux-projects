#ifndef __MQ3_APP_H__
#define __MQ3_APP_H__

typedef struct mq3_context mq3_context_t;

extern mq3_context_t *mq3_init(void);
extern int           mq3_read_raw(mq3_context_t *ctx);
extern void          mq3_deinit(mq3_context_t *ctx);

#endif
