#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <asm/delay.h>
#include <linux/spinlock.h>

extern void msleep(unsigned int msecs);

static int dht11gpiono = 0;
static spinlock_t lock;

static void dht11_idle(void)
{
    gpio_direction_output(dht11gpiono, 1);
}

static void dht11_start(void)
{
    gpio_direction_output(dht11gpiono, 1);
    gpio_set_value(dht11gpiono, 0);
    msleep(20);
    gpio_set_value(dht11gpiono, 1);
}

static int dht11_reply(void)
{
    int timeout = 0;

    gpio_direction_input(dht11gpiono);
    while (gpio_get_value(dht11gpiono) && timeout <= 10) {
        udelay(10);
        timeout++;
    }

    if (timeout > 10) {
        return -1;
    }

    while (!gpio_get_value(dht11gpiono))
        ;

    return 0;
}

static void dht11_readdata(unsigned char *pdata, int len)
{
    int n = 0;
    unsigned char tmp = 0;
    int i = 0;
    int cnt = 0;

    while (gpio_get_value(dht11gpiono))
        ;

    for (cnt = 0; cnt < len; cnt++) {
        tmp = 0;
        for (i = 7; i >= 0; i--) {
            n = 0;
            while (!gpio_get_value(dht11gpiono))
                ;
            while (gpio_get_value(dht11gpiono)) {
                udelay(10);
                n++;
            }
            if (n > 5) {
                tmp |= 0x1 << i;
            }
        }
        pdata[cnt] = tmp;
    }

    dht11_idle();
}

static int dht11_checkdata(unsigned char *pdata, int len)
{
    int i = 0;
    int sum = 0;

    if (pdata == NULL || len != 5) {
        return -2;
    }

    for (i = 0; i < 4; i++) {
        sum += pdata[i];
    }

    if ((sum & 0xff) != pdata[4]) {
        return -2;
    }

    return 0;
}

static ssize_t dht11_read(struct file *fp, char __user *puser, size_t n, loff_t *off)
{
    int ret = 0;
    unsigned char data[5] = {0};
    unsigned long flags = 0;

    (void)fp;
    (void)off;

    if (n < sizeof(data)) {
        return -EINVAL;
    }

    dht11_start();

    spin_lock_irqsave(&lock, flags);
    ret = dht11_reply();
    if (ret == -1) {
        spin_unlock_irqrestore(&lock, flags);
        dht11_idle();
        return ret;
    }

    dht11_readdata(data, sizeof(data));
    spin_unlock_irqrestore(&lock, flags);

    ret = dht11_checkdata(data, sizeof(data));
    if (ret != 0) {
        dht11_idle();
        return ret;
    }

    ret = copy_to_user(puser, data, sizeof(data));
    if (ret != 0) {
        pr_info("copy_to_user failed\n");
        dht11_idle();
        return -EFAULT;
    }

    return sizeof(data);
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = dht11_read,
};

static struct miscdevice dht11_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "dht11_misc",
    .fops = &fops,
};

static int dht11_probe(struct platform_device *pdevice)
{
    int ret = 0;

    ret = misc_register(&dht11_misc);
    if (ret != 0) {
        pr_info("misc register failed\n");
        return -1;
    }

    spin_lock_init(&lock);

    dht11gpiono = of_get_named_gpio(pdevice->dev.of_node, "gpio-dht11", 0);
    if (dht11gpiono < 0) {
        pr_info("of_get_named_gpio failed\n");
        misc_deregister(&dht11_misc);
        return -1;
    }

    ret = devm_gpio_request(dht11_misc.this_device, dht11gpiono, "dht11_drv");
    if (ret != 0) {
        pr_info("devm_gpio_request failed\n");
        misc_deregister(&dht11_misc);
        return -1;
    }

    dht11_idle();

    pr_info("dht11 probe ok!\n");

    return 0;
}

static int dht11_remove(struct platform_device *pdevice)
{
    int ret = 0;

    (void)pdevice;
    ret = misc_deregister(&dht11_misc);
    if (ret != 0) {
        pr_info("misc misc_deregister failed\n");
        return -1;
    }

    pr_info("dht11 remove ok!\n");

    return 0;
}

static struct of_device_id dht11_of_match_table[] = {
    {.compatible = "pute,putedht11"},
    {},
};

static struct platform_device_id dht11_id_table[] = {
    {.name = "putedht11"},
    {},
};

static struct platform_driver dht11_drv = {
    .probe = dht11_probe,
    .remove = dht11_remove,
    .driver = {
        .name = "putedht11",
        .of_match_table = dht11_of_match_table,
    },
    .id_table = dht11_id_table,
};

static int __init dht11_drv_init(void)
{
    int ret = 0;

    ret = platform_driver_register(&dht11_drv);
    if (ret != 0) {
        pr_info("dht11_drv_init failed\n");
        return ret;
    }

    pr_info("dht11_drv_init success!\n");

    return 0;
}

static void __exit dht11_drv_exit(void)
{
    platform_driver_unregister(&dht11_drv);

    pr_info("dht11_drv_exit success!\n");
}

module_init(dht11_drv_init);
module_exit(dht11_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pute");
