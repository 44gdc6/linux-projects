#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>

#include "adxl345_drv.h"

#define ADXL345_DEVICE_NAME "adxl345_misc"
#define ADXL345_REG_DEVID 0x00
#define ADXL345_REG_POWER_CTL 0x2D
#define ADXL345_REG_DATA_FORMAT 0x31
#define ADXL345_REG_DATAX0 0x32
#define ADXL345_DEVID_VALUE 0xE5

static int adxl345_read_reg(adxl345_drv_t *adxl, u8 reg, u8 *value)
{
    u8 txbuf[1] = { (u8)(reg | 0x80) };
    int ret = 0;

    ret = spi_write_then_read(adxl->spi, txbuf, sizeof(txbuf), value, 1);
    if (ret != 0) {
        dev_err(adxl->dev, "read reg %#x failed: %d\n", reg, ret);
    }

    return ret;
}

static int adxl345_write_reg(adxl345_drv_t *adxl, u8 reg, u8 value)
{
    u8 txbuf[2] = { reg, value };
    int ret = 0;

    ret = spi_write_then_read(adxl->spi, txbuf, sizeof(txbuf), NULL, 0);
    if (ret != 0) {
        dev_err(adxl->dev, "write reg %#x failed: %d\n", reg, ret);
    }

    return ret;
}

static int adxl345_read_xyz(adxl345_drv_t *adxl, short xyz[3])
{
    u8 txbuf[1] = { (u8)(ADXL345_REG_DATAX0 | 0x80 | 0x40) };
    u8 rxbuf[6] = {0};
    int ret = 0;

    ret = spi_write_then_read(adxl->spi, txbuf, sizeof(txbuf), rxbuf, sizeof(rxbuf));
    if (ret != 0) {
        dev_err(adxl->dev, "read xyz failed: %d\n", ret);
        return ret;
    }

    xyz[0] = (short)((rxbuf[1] << 8) | rxbuf[0]);
    xyz[1] = (short)((rxbuf[3] << 8) | rxbuf[2]);
    xyz[2] = (short)((rxbuf[5] << 8) | rxbuf[4]);

    return 0;
}

static int adxl345_hw_init(adxl345_drv_t *adxl)
{
    u8 devid = 0;
    int ret = 0;

    ret = adxl345_read_reg(adxl, ADXL345_REG_DEVID, &devid);
    if (ret != 0) {
        return ret;
    }

    if (devid != ADXL345_DEVID_VALUE) {
        dev_err(adxl->dev, "unexpected adxl345 devid: %#x\n", devid);
        return -ENODEV;
    }

    ret = adxl345_write_reg(adxl, ADXL345_REG_DATA_FORMAT, 0x08);
    if (ret != 0) {
        return ret;
    }

    ret = adxl345_write_reg(adxl, ADXL345_REG_POWER_CTL, 0x08);
    if (ret != 0) {
        return ret;
    }

    dev_info(adxl->dev, "adxl345 initialized, devid=%#x\n", devid);
    return 0;
}

static int adxl345_open(struct inode *inode, struct file *file)
{
    adxl345_drv_t *adxl = container_of(file->private_data, adxl345_drv_t, miscdev);

    file->private_data = adxl;
    dev_info(adxl->dev, "adxl345 device opened\n");
    return 0;
}

static int adxl345_release(struct inode *inode, struct file *file)
{
    adxl345_drv_t *adxl = file->private_data;

    if (adxl != NULL) {
        dev_info(adxl->dev, "adxl345 device closed\n");
    }

    return 0;
}

static ssize_t adxl345_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    adxl345_drv_t *adxl = file->private_data;
    short xyz[3] = {0};
    int ret = 0;

    if (adxl == NULL) {
        return -ENODEV;
    }

    if (count < sizeof(xyz)) {
        return -EINVAL;
    }

    (void)ppos;

    mutex_lock(&adxl->lock);
    ret = adxl345_read_xyz(adxl, xyz);
    mutex_unlock(&adxl->lock);
    if (ret != 0) {
        return ret;
    }

    if (copy_to_user(user_buf, xyz, sizeof(xyz)) != 0) {
        return -EFAULT;
    }

    return sizeof(xyz);
}

static const struct file_operations adxl345_fops = {
    .owner = THIS_MODULE,
    .open = adxl345_open,
    .release = adxl345_release,
    .read = adxl345_read,
};

static int adxl345_probe(struct spi_device *spi)
{
    adxl345_drv_t *adxl = NULL;
    int ret = 0;

    adxl = devm_kzalloc(&spi->dev, sizeof(*adxl), GFP_KERNEL);
    if (adxl == NULL) {
        return -ENOMEM;
    }

    adxl->dev = &spi->dev;
    adxl->spi = spi;
    mutex_init(&adxl->lock);

    ret = adxl345_hw_init(adxl);
    if (ret != 0) {
        mutex_destroy(&adxl->lock);
        return ret;
    }

    adxl->miscdev.minor = MISC_DYNAMIC_MINOR;
    adxl->miscdev.name = ADXL345_DEVICE_NAME;
    adxl->miscdev.fops = &adxl345_fops;
    adxl->miscdev.parent = &spi->dev;

    ret = misc_register(&adxl->miscdev);
    if (ret != 0) {
        dev_err(&spi->dev, "misc_register failed: %d\n", ret);
        mutex_destroy(&adxl->lock);
        return ret;
    }

    spi_set_drvdata(spi, adxl);
    dev_info(&spi->dev, "adxl345 probe success, device /dev/%s ready\n", ADXL345_DEVICE_NAME);
    return 0;
}

static int adxl345_remove(struct spi_device *spi)
{
    adxl345_drv_t *adxl = spi_get_drvdata(spi);

    if (adxl != NULL) {
        misc_deregister(&adxl->miscdev);
        mutex_destroy(&adxl->lock);
        dev_info(&spi->dev, "adxl345 removed\n");
    }

    return 0;
}

static const struct spi_device_id adxl345_id_table[] = {
    { .name = "puteadxl345" },
    { }
};
MODULE_DEVICE_TABLE(spi, adxl345_id_table);

static const struct of_device_id adxl345_of_match_table[] = {
    { .compatible = "pute,puteadxl345" },
    { }
};
MODULE_DEVICE_TABLE(of, adxl345_of_match_table);

static struct spi_driver adxl345_driver = {
    .id_table = adxl345_id_table,
    .probe = adxl345_probe,
    .remove = adxl345_remove,
    .driver = {
        .name = "puteadxl345",
        .owner = THIS_MODULE,
        .of_match_table = adxl345_of_match_table,
    },
};

module_spi_driver(adxl345_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pute");
MODULE_DESCRIPTION("ADXL345 SPI misc driver");
