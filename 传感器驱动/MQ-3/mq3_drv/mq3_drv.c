#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/iio/consumer.h>

#include "mq3_drv.h"

#define MQ3_DEVICE_NAME "mq3_misc"

static int mq3_open(struct inode *inode, struct file *file)
{
    mq3_drv_t *mq3 = container_of(file->private_data, mq3_drv_t, miscdev);

    file->private_data = mq3;
    dev_info(mq3->dev, "mq3 device opened\n");
    return 0;
}

static int mq3_release(struct inode *inode, struct file *file)
{
    mq3_drv_t *mq3 = file->private_data;

    if (mq3 != NULL) {
        dev_info(mq3->dev, "mq3 device closed\n");
    }

    return 0;
}

static ssize_t mq3_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    mq3_drv_t *mq3 = file->private_data;
    int raw_value = 0;
    int ret = 0;

    if (mq3 == NULL) {
        return -ENODEV;
    }

    if (count < sizeof(raw_value)) {
        return -EINVAL;
    }

    (void)ppos;

    mutex_lock(&mq3->lock);
    ret = iio_read_channel_raw(mq3->channel, &raw_value);
    if (ret == 0) {
        mq3->last_raw = raw_value;
    }
    mutex_unlock(&mq3->lock);

    if (ret < 0) {
        dev_err(mq3->dev, "iio_read_channel_raw failed: %d\n", ret);
        return ret;
    }

    if (copy_to_user(user_buf, &raw_value, sizeof(raw_value)) != 0) {
        return -EFAULT;
    }

    return sizeof(raw_value);
}

static const struct file_operations mq3_fops = {
    .owner = THIS_MODULE,
    .open = mq3_open,
    .release = mq3_release,
    .read = mq3_read,
};

static int mq3_probe(struct platform_device *pdev)
{
    mq3_drv_t *mq3 = NULL;
    int ret = 0;

    mq3 = devm_kzalloc(&pdev->dev, sizeof(*mq3), GFP_KERNEL);
    if (mq3 == NULL) {
        return -ENOMEM;
    }

    mq3->dev = &pdev->dev;
    mutex_init(&mq3->lock);

    mq3->channel = iio_channel_get(&pdev->dev, "mq3");
    if (IS_ERR(mq3->channel)) {
        ret = PTR_ERR(mq3->channel);
        dev_err(&pdev->dev, "get mq3 iio channel failed: %d\n", ret);
        return ret;
    }

    mq3->miscdev.minor = MISC_DYNAMIC_MINOR;
    mq3->miscdev.name = MQ3_DEVICE_NAME;
    mq3->miscdev.fops = &mq3_fops;
    mq3->miscdev.parent = &pdev->dev;

    ret = misc_register(&mq3->miscdev);
    if (ret != 0) {
        dev_err(&pdev->dev, "misc_register failed: %d\n", ret);
        iio_channel_release(mq3->channel);
        return ret;
    }

    platform_set_drvdata(pdev, mq3);

    dev_info(&pdev->dev, "mq3 probe success, device /dev/%s ready\n", MQ3_DEVICE_NAME);
    return 0;
}

static int mq3_remove(struct platform_device *pdev)
{
    mq3_drv_t *mq3 = platform_get_drvdata(pdev);

    if (mq3 != NULL) {
        misc_deregister(&mq3->miscdev);
        iio_channel_release(mq3->channel);
        mutex_destroy(&mq3->lock);
        dev_info(&pdev->dev, "mq3 removed\n");
    }

    return 0;
}

static const struct of_device_id mq3_of_match[] = {
    { .compatible = "pute,putemq3" },
    { }
};
MODULE_DEVICE_TABLE(of, mq3_of_match);

static struct platform_driver mq3_driver = {
    .probe = mq3_probe,
    .remove = mq3_remove,
    .driver = {
        .name = "putemq3",
        .owner = THIS_MODULE,
        .of_match_table = mq3_of_match,
    },
};

module_platform_driver(mq3_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pute");
MODULE_DESCRIPTION("MQ-3 alcohol sensor ADC misc driver");
