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

static void dht11_start(void)
{
    gpio_direction_output(dht11gpiono, 1);
    gpio_set_value(dht11gpiono, 0);
    msleep(20);
    gpio_set_value(dht11gpiono, 1);

    return;
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

    while (!gpio_get_value(dht11gpiono));

    return 0;
}

void dht11_readdata(unsigned char *pdata, int len)
{
    int n = 0;
    unsigned char tmp = 0;
    int i = 0;
    int cnt = 0;

    while (gpio_get_value(dht11gpiono));

    for (cnt = 0; cnt < 5; cnt++) {
        tmp = 0;
        for (i = 7; i >= 0; i--) {
            n = 0;
            while (!gpio_get_value(dht11gpiono));
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

    gpio_direction_output(dht11gpiono, 1);

    return;
}

static int dht11_checkdata(char *pdata, int len)
{
    int i = 0;
    int sum = 0;

    for (i = 0; i < 4; i++) {
        sum += pdata[i];
    }
    
    if (sum != pdata[4]) {
        return -2;
    }

    return 0; 
}

static ssize_t dht11_read(struct file *fp, char __user *puser, size_t n, loff_t *off)
{
    int ret = 0;
    unsigned char data[5] = {0};
    unsigned long flags = 0;

    //发送起始信号
    dht11_start();

    spin_lock_irqsave(&lock, flags);
    //接收相应信号
    ret = dht11_reply();
    if (-1 == ret) {
        return ret;
    }

    //接收40位数据
    dht11_readdata(data, sizeof(data));
    spin_unlock_irqrestore(&lock, flags);

    //校验数据
    ret = dht11_checkdata(data, sizeof(data));
    if (ret != 0) {
        return ret;
    }
    
    //传递给用户层
    ret = copy_to_user(puser, data, sizeof(data));
    if (ret != 0) {
        pr_info("copy_to_user failed\n");
        return -3;
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
        return -1;
    }

    ret = devm_gpio_request(dht11_misc.this_device, dht11gpiono, "dht11_drv");
    if (ret != 0) {
        pr_info("devm_gpio_request failed\n");
        return -1;
    }

    gpio_direction_output(dht11gpiono, 1);

    pr_info("dht11 probe ok!\n");

    return 0;
}

static int dht11_remove(struct platform_device *pdevice)
{
    int ret = 0;

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
    platform_driver_register(&dht11_drv);

    pr_info("led_drv_init success!\n");
   
    return 0;
}

static void __exit dht11_drv_exit(void)
{
    platform_driver_unregister(&dht11_drv);

    pr_info("led_drv_exit success!\n");

    return;
}

module_init(dht11_drv_init);
module_exit(dht11_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pute");