#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "beep_drv.h"
#include <linux/gpio.h>
#include <linux/of_gpio.h>

static beep_drv_t *pbeepcfg; 

static ssize_t beep_show(struct device *dev, struct device_attribute *attr, char *buf)
{   
    ssize_t len = 0;

    (void)dev;
    (void)attr;

    if (pbeepcfg == NULL) {
        return -ENODEV;
    }

    mutex_lock(&pbeepcfg->lock);
    if (BEEP_ON == pbeepcfg->curbeepstat) {
        len = scnprintf(buf, PAGE_SIZE, "BEEP_ON\n");
    } else {
        len = scnprintf(buf, PAGE_SIZE, "BEEP_OFF\n");
    }
    mutex_unlock(&pbeepcfg->lock);

    return len;
}

static ssize_t beep_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    char tmpbuff[32] = {0};

    (void)dev;
    (void)attr;

    if (pbeepcfg == NULL) {
        return -ENODEV;
    }

    if (sscanf(buf, "%31s", tmpbuff) != 1) {
        return -EINVAL;
    }

    mutex_lock(&pbeepcfg->lock);
    if (0 == strcmp(tmpbuff, "BEEP_ON")) {
        gpio_set_value(pbeepcfg->gpiono, 0);
        pbeepcfg->curbeepstat = BEEP_ON;
    } else if (0 == strcmp(tmpbuff, "BEEP_OFF")) {
        gpio_set_value(pbeepcfg->gpiono, 1);
        pbeepcfg->curbeepstat = BEEP_OFF;
    } else {
        mutex_unlock(&pbeepcfg->lock);
        return -EINVAL;
    }
    mutex_unlock(&pbeepcfg->lock);

    return count;
}

static struct device_attribute beep_attr = {
    .attr = {
        .name = "attr",
        .mode = 0664,
    },
    .show = beep_show,
    .store = beep_store,
};

static int beep_open(struct inode *node, struct file *fp)
{
    (void)node;
    (void)fp;
    pr_info("beep device opened\n");

    return 0;
}

static int beep_close(struct inode *node, struct file *fp)
{
    (void)node;
    (void)fp;
    pr_info("beep device closed\n");

    return 0;
}

static ssize_t beep_read(struct file *fp, char __user *puser, size_t n, loff_t *off)
{
    int state = 0;

    (void)fp;
    (void)off;

    if (pbeepcfg == NULL) {
        return -ENODEV;
    }

    if (n < sizeof(state)) {
        return -EINVAL;
    }

    mutex_lock(&pbeepcfg->lock);
    state = pbeepcfg->curbeepstat;
    mutex_unlock(&pbeepcfg->lock);

    if (copy_to_user(puser, &state, sizeof(state)) != 0) {
        pr_err("beep copy_to_user failed\n");
        return -EFAULT;
    }

    pr_info("beep state read\n");

    return sizeof(state);
}

static ssize_t beep_write(struct file *fp, const char __user *puser, size_t n, loff_t *off)
{
    int setstat = 0;

    (void)fp;
    (void)off;

    if (pbeepcfg == NULL) {
        return -ENODEV;
    }

    if (n < sizeof(setstat)) {
        return -EINVAL;
    }

    if (copy_from_user(&setstat, puser, sizeof(setstat)) != 0) {
        pr_err("beep copy_from_user failed\n");
        return -EFAULT;
    }

    mutex_lock(&pbeepcfg->lock);
    if (BEEP_ON == setstat) {
        gpio_set_value(pbeepcfg->gpiono, 0);
        pbeepcfg->curbeepstat = BEEP_ON;
    } else if (BEEP_OFF == setstat) {
        gpio_set_value(pbeepcfg->gpiono, 1);
        pbeepcfg->curbeepstat = BEEP_OFF;
    } else {
        mutex_unlock(&pbeepcfg->lock);
        return -EINVAL;
    }
    mutex_unlock(&pbeepcfg->lock);

    pr_info("beep state updated\n");

    return sizeof(setstat);
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = beep_open,
    .release = beep_close,
    .read = beep_read,
    .write = beep_write,
};

static struct miscdevice beep_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "beep_misc",
    .fops = &fops,
};

static int __init beep_drv_init(void)
{
    int ret = 0;

    pbeepcfg = kmalloc(sizeof(*pbeepcfg), GFP_KERNEL);
    if (NULL == pbeepcfg) {
        pr_err("kmalloc pbeepcfg failed\n");
        return -ENOMEM;
    }

    pbeepcfg->pbeepmisc = &beep_misc;

    ret = misc_register(&beep_misc);
    if (ret != 0) {
        pr_err("beep misc_register failed: %d\n", ret);
        goto err_mis_register;
    }

    mutex_init(&pbeepcfg->lock);

    pbeepcfg->pnode = of_find_node_by_path("/putebeep");
    if (NULL == pbeepcfg->pnode) {
        pr_err("of_find_node_by_path failed\n");
        ret = -ENODEV;
        goto err_find_resource;
    }

    pbeepcfg->gpiono = of_get_named_gpio(pbeepcfg->pnode, "gpio-beep", 0);
    if (pbeepcfg->gpiono < 0) {
        pr_err("get gpio-beep failed: %d\n", pbeepcfg->gpiono);
        ret = pbeepcfg->gpiono;
        goto err_find_resource;
    }

    ret = devm_gpio_request(beep_misc.this_device, pbeepcfg->gpiono, "beep_drv");
    if (ret != 0) {
        pr_err("devm_gpio_request failed: %d\n", ret);
        goto err_find_resource;
    }

    gpio_direction_output(pbeepcfg->gpiono, 1);
    pbeepcfg->curbeepstat = BEEP_OFF;

    ret = device_create_file(beep_misc.this_device, &beep_attr);
    if (ret != 0) {
        pr_err("device_create_file failed: %d\n", ret);
        goto err_find_resource;
    }

    pr_info("beep probe success, device /dev/%s ready\n", beep_misc.name);

    return 0;

err_find_resource:
    if (pbeepcfg->pnode != NULL) {
        of_node_put(pbeepcfg->pnode);
    }
    mutex_destroy(&pbeepcfg->lock);
    misc_deregister(&beep_misc);
err_mis_register:
    kfree(pbeepcfg);
    pbeepcfg = NULL;
    return ret;
}

static void __exit beep_drv_exit(void)
{
    if (pbeepcfg == NULL) {
        return;
    }

    device_remove_file(beep_misc.this_device, &beep_attr);

    mutex_destroy(&pbeepcfg->lock);

    if (pbeepcfg->pnode != NULL) {
        of_node_put(pbeepcfg->pnode);
    }

    misc_deregister(&beep_misc);
    kfree(pbeepcfg);
    pbeepcfg = NULL;

    pr_info("beep driver unregistered\n");

    return;
}

module_init(beep_drv_init);
module_exit(beep_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pute");
