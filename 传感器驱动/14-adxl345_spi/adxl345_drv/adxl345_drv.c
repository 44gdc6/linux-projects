#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/spi/spi.h>

static struct spi_device *adxl345_spi = NULL;

void adxl345_init(void)
{
    //读取器件ID
    int ret = 0;
    unsigned char txbuf[2] = {0};
    unsigned char rxbuf[1] = {0};
    
    txbuf[0] = 0x00 | 0x80;
    ret = spi_write_then_read(adxl345_spi, txbuf, 1, rxbuf, 1);
    if (ret != 0) {
        pr_info("spi_write_then_read failed\n");
        return;
    }
 //   pr_info("rxbuf[0] = %#x\n", rxbuf[0]);

    //调整量程
    txbuf[0] = 0x31;
    txbuf[1] = 0x08;
    ret = spi_write_then_read(adxl345_spi, txbuf, 2, NULL, 0);
    if (ret != 0) {
        pr_info("spi_write_then_read failed\n");
        return;
    }

    //开启测量
    txbuf[0] = 0x2D;
    txbuf[1] = 0x08;
    ret = spi_write_then_read(adxl345_spi, txbuf, 2, NULL, 0);
    if (ret != 0) {
        pr_info("spi_write_then_read failed\n");
        return;
    }

    return;
}

void adxl345_readdata(short *paccx, short *paccy, short *paccz)
{
    int ret = 0;
    unsigned char txbuf[7] = {0};
    unsigned char rxbuf[7] = {0};
    struct spi_message sendmsg;
    struct spi_transfer msg_transfer = {
        .tx_buf = txbuf,
        .rx_buf = rxbuf,
        .len = 7,
        .delay_usecs = 20,
    };
    
    //开启连续读取
    txbuf[0] = 0x32 | 0x80 | 0x40;
    spi_message_init(&sendmsg);
    spi_message_add_tail(&msg_transfer, &sendmsg);
    ret = spi_sync(adxl345_spi, &sendmsg);
    if (ret != 0) {
        pr_info("spi_sync failed\n");
    }

    *paccx = (rxbuf[2] << 8 | rxbuf[1]);
    *paccy = (rxbuf[4] << 8 | rxbuf[3]);
    *paccz = (rxbuf[6] << 8 | rxbuf[5]);

    return;
}

static ssize_t adxl345_read(struct file *fp, char __user *puser, size_t n, loff_t *off)
{
    short accval[3] = {0};
    unsigned long nret = 0;

    adxl345_init();
    adxl345_readdata(&accval[0], &accval[1], &accval[2]);
    
    nret = copy_to_user(puser, accval, sizeof(accval));
    if (nret != 0) {
        pr_info("copy_to_user failed\n");
        return -1;
    }

    return sizeof(accval);
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = adxl345_read,
};

static struct miscdevice adxl345_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "adxl345_misc",
    .fops = &fops,
};

static int adxl345_probe(struct spi_device *spi)
{
    int ret = 0;
    
    adxl345_spi = spi;
    ret = misc_register(&adxl345_misc);
    if (ret != 0) {
        pr_info("misc_register failed\n");
        return -1;
    }

    pr_info("max_speed_hz:%d\n", spi->max_speed_hz);
    pr_info("chip_select:%d\n", spi->chip_select);
    pr_info("bits_per_word:%d\n", spi->bits_per_word);
    pr_info("mode:%d\n", spi->mode);

    pr_info("adxl345 probe ok!\n");

    return 0;
}

static int adxl345_remove(struct spi_device *spi)
{
    int ret = 0;
    
    ret = misc_deregister(&adxl345_misc);
    if (ret != 0) {
        pr_info("misc_deregister failed\n");
        return -1;
    }

    pr_info("adxl345 remove ok!\n");

    return 0;
}

static struct spi_device_id adxl345_id_table[] = {
    {.name = "puteadxl345"},
    {},
};
        
static struct of_device_id adxl345_of_match_table[] = {
    {.compatible = "pute,puteadxl345"},
    {},
};

static struct spi_driver adxl345_drv = {
    .id_table = adxl345_id_table,
    .probe = adxl345_probe,
    .remove = adxl345_remove,
    .driver = {
        .name = "puteadxl345",
        .owner = THIS_MODULE,
        .of_match_table = adxl345_of_match_table,
    },
};

static int __init adxl345_drv_init(void)
{
    int ret = 0;

    ret = spi_register_driver(&adxl345_drv);
    if (ret != 0) {
        pr_info("spi_register driver failed\n");
        return -1;
    }

    return 0;
}

static void __exit adxl345_drv_exit(void)
{
    spi_unregister_driver(&adxl345_drv);

    return;
}

module_init(adxl345_drv_init);
module_exit(adxl345_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pute");