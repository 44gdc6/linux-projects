#include "core/mailbox.h"
#include "core/linklist.h"
#include "core/linkqueue.h"

Node_t *pmailbox = NULL;

int mailbox_init(void)
{
    pmailbox = CreateEmptyLinkList();
    
    return 0;
}

int mailbox_add_task(const char *pthreadname, void *(*pthreadfun)(void *arg))
{
    DataType tmptask;

    tmptask.pthreadfun = pthreadfun;
    tmptask.queue = linkcreate_empty_linkqueue();
    strcpy(tmptask.threadname, pthreadname);

    pthread_create(&tmptask.tid, NULL, pthreadfun, NULL);

    InsertTailLinkNode(pmailbox, tmptask);

    return 0;
}

int mailbox_send_msg(const char *pthreadname, data_t tmpdata)
{
    Node_t *ptmptask = NULL;

    ptmptask = FindLinkNode(pmailbox, pthreadname);
    if (NULL == ptmptask)
    {
        return -1;
    }

    enter_linkqueue(ptmptask->Data.queue, tmpdata);

    return 0;
}

int mailbox_recv_msg(data_t *ptmpdata)
{
    pthread_t tmpthread;
    Node_t *ptmptask = NULL;

    tmpthread = pthread_self();

    ptmptask = FindLinkNodeById(pmailbox, tmpthread);
    if (NULL == ptmptask)
    {
        return -1;
    }

    if (!is_empty_linkqueue(ptmptask->Data.queue))
    {
        quit_linkqueue(ptmptask->Data.queue, ptmpdata);
        return 1;
    }

    return 0;
}

int mailbox_waitall_thread_end(void)
{
    Node_t *ptmpnode = NULL;

    ptmpnode = pmailbox->pNext;
    while (ptmpnode != NULL)
    {
        pthread_join(ptmpnode->Data.tid, NULL);
        ptmpnode = ptmpnode->pNext;
    }

    return 0;
}

int mailbox_destroy(void)
{
    Node_t *ptmpnode = NULL;

    ptmpnode = pmailbox->pNext;
    while (ptmpnode != NULL)
    {
        destroy_linkqueue(&ptmpnode->Data.queue);
        ptmpnode = ptmpnode->pNext;
    }
    DestroyLinkList(&pmailbox);

    return 0;
}
