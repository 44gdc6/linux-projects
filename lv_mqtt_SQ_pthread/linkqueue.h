#ifndef __LINKQUEUE_H__
#define __LINKQUEUE_H__

#include <time.h>

#include "sensor_common.h"

typedef struct datatype {
    double temp;
    double hum;
    double light;
    double smoke;
    int temp_valid;
    temp_source_t temp_source;
    time_t sample_time;
} data_t;

typedef struct node {
    data_t data;
    struct node *pnext;
} node_t;

typedef struct linkqueue {
    node_t *phead;
    node_t *ptail;
} linkqueue_t;

extern linkqueue_t *linkcreate_empty_linkqueue(void);
extern int is_empty_linkqueue(linkqueue_t *ptmpqueue);
extern int enter_linkqueue(linkqueue_t *ptmpqueue, data_t tmpdata);
extern int quit_linkqueue(linkqueue_t *ptmpqueue, data_t *ptmpdata);
extern int destroy_linkqueue(linkqueue_t **pptmpqueue);

#endif
