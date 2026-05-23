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

    memset(&sendmsg, 0, sizeof(sendmsg));
    memset(&recvmsg, 0, sizeof(recvmsg));
    sendmsg.addr = 0x48;
    tmpbuff[0] = 0x00;
    sendmsg.buf = tmpbuff;
    sendmsg.len = 1;
    plm75client->adapter->algo->master_xfer(plm75client->adapter, &sendmsg, 1);

    recvmsg.addr = 0x48;
    recvmsg.flags |= I2C_M_RD;
    memset(tmpbuff, 0, sizeof(tmpbuff));
    recvmsg.buf = tmpbuff;
    recvmsg.len = 2;
    plm75client->adapter->algo->master_xfer(plm75client->adapter, &recvmsg, 1);

    tmpval = ((tmpbuff[0] << 8 | tmpbuff[1]) >> 7);
    nret = copy_to_user(puser, &tmpval, sizeof(tmpval));
    if (nret != 0) {
        pr_info("copy_to_user failed\n");
    }

    return 0;
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
        pr_info("misc_register failed\n");
        return -1;
    }

    return 0;
}

static int lm75_remove(struct i2c_client *pclient)
{
    int ret = 0;
    
    ret = misc_deregister(&lm75_misc);
    if (ret != 0) {
        pr_info("misc_deregister failed\n");
        return -1;
    }

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