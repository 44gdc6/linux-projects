#ifndef __LINKQUEUE_H__
#define __LINKQUEUE_H__

#include <time.h>

#include "device/sensor_common.h"
#include "device/weather_client.h"

typedef struct datatype {
    double temp;
    double hum;
    int hum_valid;
    double light;
    double smoke;
    int alcohol_raw;
    int alcohol_valid;
    int alcohol_level;
    int alcohol_alarm;
    int accel_x;
    int accel_y;
    int accel_z;
    int accel_valid;
    int motion_state;
    int motion_alarm;
    int weather_valid;
    char weather_day[WEATHER_FIELD_LEN];
    char weather_week[WEATHER_FIELD_LEN];
    char weather_city[WEATHER_FIELD_LEN];
    char weather_text[WEATHER_FIELD_LEN];
    char weather_temp[WEATHER_FIELD_LEN];
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
