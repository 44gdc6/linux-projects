#ifndef __ADXL345_DRV_H__
#define __ADXL345_DRV_H__

#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>

typedef struct adxl345_drv {
    struct device *dev;
    struct spi_device *spi;
    struct miscdevice miscdev;
    struct mutex lock;
} adxl345_drv_t;

#endif
