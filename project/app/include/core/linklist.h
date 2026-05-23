#ifndef __LINKLIST_H__
#define __LINKLIST_H__

#include <pthread.h>
#include "core/linkqueue.h"

typedef struct mailbox {
    pthread_t tid;
    void *(*pthreadfun)(void *arg);
    char threadname[32];
    linkqueue_t *queue;
}mailbox_t;

typedef mailbox_t DataType;

typedef struct listnode 
{
	DataType Data;
	struct listnode *pNext;
}Node_t;

extern Node_t *CreateEmptyLinkList(void);
extern int InsertHeadLinkNode(Node_t *pHead, DataType TmpData);
extern int DeleteLinkNode(Node_t *pHead, const char *pthreadname);
extern Node_t *FindLinkNode(Node_t *pHead, const char *pthreadname);
extern int InsertTailLinkNode(Node_t *pHead, DataType TmpData);
extern int DestroyLinkList(Node_t **ppHead);
extern Node_t *FindLinkNodeById(Node_t *pHead, pthread_t tid);

#endif
