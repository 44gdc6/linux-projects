#include "core/linkqueue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

linkqueue_t *linkcreate_empty_linkqueue(void)
{
    linkqueue_t *ptmpqueue = NULL;

    ptmpqueue = malloc(sizeof(linkqueue_t));
    if (NULL == ptmpqueue) 
    {
        perror("fail to malloc");
        return NULL;
    }

    ptmpqueue->phead = malloc(sizeof(node_t));
    if (NULL == ptmpqueue->phead)
    {
        perror("fail to malloc");
        return NULL;
    }

    ptmpqueue->ptail = ptmpqueue->phead;
    ptmpqueue->phead->pnext = NULL;

    return ptmpqueue;
}

int is_empty_linkqueue(linkqueue_t *ptmpqueue)
{
    return (ptmpqueue->phead == ptmpqueue->ptail ? 1 : 0);
}

int enter_linkqueue(linkqueue_t *ptmpqueue, data_t tmpdata)
{
    node_t *pnewnode = NULL;

    pnewnode = malloc(sizeof(node_t));
    if (NULL == pnewnode)
    {
        perror("fail to malloc");
        return -1;
    }

    pnewnode->data = tmpdata;
    pnewnode->pnext = NULL;
    ptmpqueue->ptail->pnext = pnewnode;
    ptmpqueue->ptail = pnewnode;

    return 0;
}

int quit_linkqueue(linkqueue_t *ptmpqueue, data_t *ptmpdata)
{
    node_t *pfreenode = NULL;

    pfreenode = ptmpqueue->phead->pnext;
    *ptmpdata = pfreenode->data;
    ptmpqueue->phead->pnext = pfreenode->pnext;
    if (NULL == pfreenode->pnext)
    {
        ptmpqueue->ptail = ptmpqueue->phead;
    }
    free(pfreenode);

    return 0;
}

int destroy_linkqueue(linkqueue_t **pptmpqueue)
{
    node_t *ptmpnode = NULL;
    node_t *pfreenode = NULL;

    ptmpnode = pfreenode = (*pptmpqueue)->phead;
    while (ptmpnode != NULL)
    {
        ptmpnode = ptmpnode->pnext;
        free(pfreenode);
        pfreenode = ptmpnode;
    }
    free((*pptmpqueue));
    *pptmpqueue = NULL;

    return 0;
}
