#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>

static struct i2c_client *plm75client;

static ssize_t lm75_read(struct file *fp, char __user *puser, size_t n, loff_t *off)
{
    struct i2c_msg sendmsg;
    struct i2c_msg recvmsg;
    unsigned long nret = 0;
    unsigned char tmpbuff[2] = {0};
    unsigned short tmpval = 0;
    int ret;

    /* 参数检查 */
    if (n < sizeof(tmpval))
        return -EINVAL;

    memset(&sendmsg, 0, sizeof(sendmsg));
    memset(&recvmsg, 0, sizeof(recvmsg));

    /* 发送寄存器地址 */
    sendmsg.addr = 0x48;
    tmpbuff[0] = 0x00;
    sendmsg.buf = tmpbuff;
    sendmsg.len = 1;
    ret = plm75client->adapter->algo->master_xfer(plm75client->adapter, &sendmsg, 1);
    if (ret < 0) {
        pr_err("lm75: i2c send failed: %d\n", ret);
        return ret;
    }

    /* 读取温度数据 */
    recvmsg.addr = 0x48;
    recvmsg.flags |= I2C_M_RD;
    memset(tmpbuff, 0, sizeof(tmpbuff));
    recvmsg.buf = tmpbuff;
    recvmsg.len = 2;
    ret = plm75client->adapter->algo->master_xfer(plm75client->adapter, &recvmsg, 1);
    if (ret < 0) {
        pr_err("lm75: i2c recv failed: %d\n", ret);
        return ret;
    }

    /* 解析温度值 */
    tmpval = ((tmpbuff[0] << 8 | tmpbuff[1]) >> 7);

    /* 拷贝到用户空间 */
    nret = copy_to_user(puser, &tmpval, sizeof(tmpval));
    if (nret != 0) {
        pr_err("lm75: copy_to_user failed: %lu\n", nret);
        return -EFAULT;
    }

    return sizeof(tmpval);
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = lm75_read,
};

static struct miscdevice lm75_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "lm75_misc",
    .fops = &fops,
};

static int lm75_probe(struct i2c_client *pclient, const struct i2c_device_id *pid)
{
    int ret = 0;

    plm75client = pclient;
    ret = misc_register(&lm75_misc);
    if (ret != 0) {
        pr_err("lm75: misc_register failed: %d\n", ret);
        return ret;
    }

    pr_info("lm75: device registered, addr=0x%02x\n", pclient->addr);
    return 0;
}

static int lm75_remove(struct i2c_client *pclient)
{
    int ret = 0;

    ret = misc_deregister(&lm75_misc);
    if (ret != 0) {
        pr_err("lm75: misc_deregister failed: %d\n", ret);
        return ret;
    }

    pr_info("lm75: device removed\n");
    return 0;
}

static struct i2c_device_id lm75_id_table[] = {
    {.name = "putelm75"},
    {},
};

static struct of_device_id  lm75_of_match_table[] = {
    {.compatible = "pute,putelm75"},
    {},
};

static struct i2c_driver lm75_driver = {
    .probe = lm75_probe,
    .remove = lm75_remove,
    .driver = {
        .name = "putelm75",
        .owner = THIS_MODULE,
        .of_match_table = lm75_of_match_table,
    },
    .id_table = lm75_id_table,
};

static int __init lm75_drv_init(void)
{
    int ret = 0;
    
    ret = i2c_register_driver(THIS_MODULE, &lm75_driver);
    if (ret != 0) {
        pr_info("i2c_register_driver failed\n");
        return -1;
    }

    return 0;
}

static void __exit lm75_drv_exit(void)
{
    i2c_del_driver(&lm75_driver);

    return;
}

module_init(lm75_drv_init);
module_exit(lm75_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pute");