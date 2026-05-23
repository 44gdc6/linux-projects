#ifndef __MAILBOX_H__
#define __MAILBOX_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "core/linklist.h"
#include "core/linkqueue.h"

extern Node_t *pmailbox;

extern int mailbox_init(void);
extern int mailbox_add_task(const char *pthreadname, void *(*pthreadfun)(void *arg));
extern int mailbox_send_msg(const char *pthreadname, data_t tmpdata);
extern int mailbox_recv_msg(data_t *ptmpdata);
extern int mailbox_waitall_thread_end(void);
extern int mailbox_destroy(void);

#endif
