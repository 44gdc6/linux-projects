#include "core/linklist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Node_t *CreateEmptyLinkList(void)
{
	Node_t *pNewNode = NULL;

	pNewNode = malloc(sizeof(Node_t));
	if (NULL == pNewNode)
	{
		perror("fail to malloc");
		return NULL;
	}

	pNewNode->pNext = NULL;
	
	return pNewNode;
}

int InsertHeadLinkNode(Node_t *pHead, DataType TmpData)
{
	Node_t *pNewNode = NULL;

	pNewNode = malloc(sizeof(Node_t));
	if (NULL == pNewNode)
	{
		perror("fail to malloc");
		return -1;
	}

	pNewNode->Data = TmpData;
	pNewNode->pNext = pHead->pNext;
	pHead->pNext = pNewNode;
	
	return 0;
}

int DeleteLinkNode(Node_t *pHead, const char *pthreadname)
{
	int Cnt = 0;
	Node_t *pTmpNode = NULL;
	Node_t *pPreNode = NULL;

	pTmpNode = pHead->pNext;
	pPreNode = pHead;

	while (pTmpNode != NULL)
	{
		if (0 == strcmp(pTmpNode->Data.threadname, pthreadname))
		{
			pPreNode->pNext = pTmpNode->pNext;
			free(pTmpNode);
			pTmpNode = pPreNode->pNext;
			Cnt++;
		}
		else 
		{
			pTmpNode = pTmpNode->pNext;
			pPreNode = pPreNode->pNext;
		}
	}

	return Cnt;
}

Node_t *FindLinkNode(Node_t *pHead, const char *pthreadname)
{
	Node_t *pTmpNode = NULL;
	
	pTmpNode = pHead->pNext;
	while (pTmpNode != NULL)
	{
		if (0 == strcmp(pTmpNode->Data.threadname, pthreadname))
		{
			return pTmpNode;
		}
		pTmpNode = pTmpNode->pNext;
	}

	return NULL;
}

Node_t *FindLinkNodeById(Node_t *pHead, pthread_t tid)
{
	Node_t *pTmpNode = NULL;
	
	pTmpNode = pHead->pNext;
	while (pTmpNode != NULL)
	{
		if (pTmpNode->Data.tid == tid)
		{
			return pTmpNode;
		}
		pTmpNode = pTmpNode->pNext;
	}

	return NULL;
}

int InsertTailLinkNode(Node_t *pHead, DataType TmpData)
{
	Node_t *pNewNode = NULL;
	Node_t *pLastNode = NULL;

	pNewNode = malloc(sizeof(Node_t));
	if (NULL == pNewNode)
	{
		perror("fail to malloc");
		return -1;
	}

	pNewNode->Data = TmpData;
	pNewNode->pNext = NULL;

	pLastNode = pHead;
	while (pLastNode->pNext != NULL)
	{
		pLastNode = pLastNode->pNext;
	}
	pLastNode->pNext = pNewNode;

	return 0;
}

int DestroyLinkList(Node_t **ppHead)
{
	Node_t *pTmpNode = NULL;
	Node_t *pFreeNode = NULL;

	pTmpNode = *ppHead;
	pFreeNode = *ppHead;
	while (pTmpNode != NULL)
	{
		pTmpNode = pTmpNode->pNext;
		free(pFreeNode);
		pFreeNode = pTmpNode;
	}
	*ppHead = NULL;

	return 0;
}

