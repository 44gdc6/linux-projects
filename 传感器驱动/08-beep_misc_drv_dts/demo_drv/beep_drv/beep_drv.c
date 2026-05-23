#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
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
    mutex_lock(&pbeepcfg->lock);
    if (BEEP_ON == pbeepcfg->curbeepstat) {
        pr_info("BEEP_ON\n");
    }
    else if (BEEP_OFF == pbeepcfg->curbeepstat) {
        pr_info("BEEP_OFF\n");
    }
    mutex_unlock(&pbeepcfg->lock);

    return 0;
}

static ssize_t beep_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    char tmpbuff[32] = {0};

    sscanf(buf, "%s", tmpbuff);

    mutex_lock(&pbeepcfg->lock);
    if (0 == strcmp(tmpbuff, "BEEP_ON")) {
        gpio_set_value(pbeepcfg->gpiono, 0);
        pbeepcfg->curbeepstat = BEEP_ON;
    } else if (0 == strcmp(tmpbuff, "BEEP_OFF")) {
        gpio_set_value(pbeepcfg->gpiono, 1);
        pbeepcfg->curbeepstat = BEEP_OFF;
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
    pr_info("Kernel:beep open success\n");

    return 0;
}

static int beep_close(struct inode *node, struct file *fp)
{
    pr_info("Kernel:beep close success\n");

    return 0;
}

static ssize_t beep_read(struct file *fp, char __user *puser, size_t n, loff_t *off)
{
    unsigned long nret = 0;
    
    nret = copy_to_user(puser, &pbeepcfg->curbeepstat, 4);
    if (nret != 0) {
        pr_info("copy_to_user failed\n");
        return -1;
    }

    pr_info("Kernel:beep read success\n");

    return 0;
}

static ssize_t beep_write(struct file *fp, const char __user *puser, size_t n, loff_t *off)
{
    int setstat = 0;
    unsigned long nret = 0;
    
    nret = copy_from_user(&setstat, puser, 4);
    if (nret != 0) {
        pr_info("copy_from_user failed\n");
        return -1;
    }

    mutex_lock(&pbeepcfg->lock);
    if (BEEP_ON == setstat) {
        gpio_set_value(pbeepcfg->gpiono, 0);
        pbeepcfg->curbeepstat = BEEP_ON;
    }
    else if (BEEP_OFF == setstat) {
        gpio_set_value(pbeepcfg->gpiono, 1);
        pbeepcfg->curbeepstat = BEEP_OFF;
    }
    mutex_unlock(&pbeepcfg->lock);

    pr_info("Kernel:beep write success\n");

    return 0;
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

    //1.构建beep信息的空间
    pbeepcfg = kmalloc(sizeof(*pbeepcfg), GFP_KERNEL);
    if (NULL == pbeepcfg) {
        pr_info("kmalloc pbeepcfg failed\n");
        goto err_kmalloc;
    }

    //2.构建对设备操作的方法
    pbeepcfg->pbeepmisc = &beep_misc;

    //3.注册混杂设备
    ret = misc_register(&beep_misc);
    if (ret != 0) {
        pr_info("misc register failed\n");
        goto err_mis_register;
    }

    //3.初始化锁资源
    mutex_init(&pbeepcfg->lock);

    //4.找到设备树中的putebeep节点
    pbeepcfg->pnode = of_find_node_by_path("/putebeep");
    if (NULL == pbeepcfg->pnode) {
        pr_info("of_find_node_by_path failed\n");
        goto err_find_resource;
    }

    //5.获取节点中gpio-beep属性对应的GPIO编号
    pbeepcfg->gpiono = of_get_named_gpio(pbeepcfg->pnode, "gpio-beep", 0);
    if (pbeepcfg->gpiono < 0) {
        pr_info("get gpio-beep failed\n");
        goto err_find_resource;
    }

    //6.注册gpio的使用权
    ret = devm_gpio_request(beep_misc.this_device, pbeepcfg->gpiono, "beep_drv");
    if (ret != 0) {
        pr_info("devm_gpio_request failed\n");
        goto err_find_resource;
    }

    //7.设置gpio输出高电平
    gpio_direction_output(pbeepcfg->gpiono, 1);
    pbeepcfg->curbeepstat = BEEP_OFF;

    //8.增加sys调节节点
    ret = device_create_file(beep_misc.this_device, &beep_attr);
    if (ret != 0) {
        pr_info("device_create_file failed\n");
        return -1;
    }

    pr_info("major:%d, minor:%d\n", MAJOR(beep_misc.this_device->devt), MINOR(beep_misc.this_device->devt));

    pr_info("beep_drv_init success!\n");

    return 0;

err_find_resource:
    misc_deregister(&beep_misc);
err_mis_register:
    kfree(pbeepcfg);
err_kmalloc:
    return -1;
}

static void __exit beep_drv_exit(void)
{
    device_remove_file(beep_misc.this_device, &beep_attr);

    mutex_destroy(&pbeepcfg->lock);

    misc_deregister(&beep_misc);
    kfree(pbeepcfg);

    pr_info("beep_drv_exit success!\n");

    return;
}

module_init(beep_drv_init);
module_exit(beep_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pute");