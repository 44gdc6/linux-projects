#ifndef __MQ3_DRV_H__
#define __MQ3_DRV_H__

#include <linux/device.h>
#include <linux/iio/consumer.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>

typedef struct mq3_drv {
    struct device *dev;
    struct iio_channel *channel;
    struct miscdevice miscdev;
    struct mutex lock;
    int last_raw;
} mq3_drv_t;

#endif
