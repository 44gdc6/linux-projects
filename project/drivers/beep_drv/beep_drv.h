#ifndef __BEEP_DRV_H__
#define __BEEP_DRV_H__

#define BEEP_ON  1
#define BEEP_OFF 0

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <linux/of.h>

typedef struct beep_drv {
    struct miscdevice *pbeepmisc;
    int curbeepstat;
    struct mutex lock;
    struct device_node *pnode;
    int gpiono;
}beep_drv_t;

#endif