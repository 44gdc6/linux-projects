#include <linux/init.h>       // module_init / module_exit 宏
#include <linux/module.h>     // MODULE_LICENSE 等
#include <linux/i2c.h>        // I2C相关 b
#include <linux/delay.h>
#include <linux/miscdevice.h>   // misc_register
#include <asm/uaccess.h>        // copy_to_user


//定义寄存器地址
#define MPU6050_REG_WHO_AM_I      0x75   // 芯片 ID
#define MPU6050_VAL_WHO_AM_I      0x68   // 正确的 ID 值
#define MPU6050_REG_PWR_MGMT_1    0x6B   // 电源管理
#define MPU6050_REG_SMPLRT_DIV    0x19   // 采样率分频
#define MPU6050_REG_CONFIG        0x1A   // 数字低通滤波
#define MPU6050_REG_GYRO_CONFIG   0x1B   // 陀螺仪量程
#define MPU6050_REG_ACCEL_CONFIG  0x1C   // 加速度计量程
#define MPU6050_REG_ACCEL_XOUT_H  0x3B   // 数据起始地址
#define MPU6050_DATA_SIZE         14     // 一次读 14 字节

static struct i2c_client *g_client;       //全局变量保存 client

struct mpu6050_data {
    short accel_x;
    short accel_y;
    short accel_z;
    short temp;      // 温度
    short gyro_x;
    short gyro_y;
    short gyro_z;
};

//读寄存器
static int mpu6050_read_reg(struct i2c_client *client, u8 reg, u8 *val)
{
    int ret;
    ret = i2c_smbus_read_byte_data(client, reg);
    if(ret < 0){
        dev_err(&client->dev, "read reg 0x%02x failed", reg);
        return ret;
    }
    *val = (u8)ret;      // 把返回值转成 u8 存到 val
    return 0;
}

//写寄存器
static int mpu6050_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
    int ret;
    ret = i2c_smbus_write_byte_data(client, reg, val);
    if(ret < 0){
        dev_err(&client->dev, "write reg 0x%02x failed\n", reg);
        return ret;
    };
    return 0;
}

//芯片初始化
static int mpu6050_chip_init(struct i2c_client *client)
{
    int ret;
    u8 val;
    /* ===== 第①步：验证芯片身份 ===== */
    ret = mpu6050_read_reg(client, MPU6050_REG_WHO_AM_I, &val);
    if(ret < 0)
        return ret;
    // val 是读回来的值，MPU6050 的 WHO_AM_I 必须是 0x68
    if(val != MPU6050_VAL_WHO_AM_I)
    {
        dev_err(&client->dev,"wrong chip ID: 0x%02x (expected 0x%02x)\n",
                val, MPU6050_VAL_WHO_AM_I);
        return -ENODEV;
    }
    dev_info(&client->dev, "MPU6050 found (WHO_AM_I = 0x%02x)\n",val);

    /* ===== 第2步：唤醒芯片 ===== */
    ret = mpu6050_write_reg(client, MPU6050_REG_PWR_MGMT_1, 0x00);
    if (ret < 0)
        return ret;
    msleep(100);  // 等 100 毫秒，让内部晶振起振稳定

    /* ===== 第③步：采样率 1kHz ===== */
    ret = mpu6050_write_reg(client, MPU6050_REG_SMPLRT_DIV, 0x07);
    if (ret < 0)
        return ret;

    /* ===== 第④步：低通滤波 44Hz ===== */
    ret = mpu6050_write_reg(client, MPU6050_REG_CONFIG, 0x03);
    if (ret < 0)
        return ret;

    /* ===== 第⑤步：陀螺仪 ±2000°/s ===== */
    ret = mpu6050_write_reg(client, MPU6050_REG_GYRO_CONFIG, 0x18);
    if (ret < 0)
        return ret;

    /* ===== 第⑥步：加速度计 ±16g ===== */
    ret = mpu6050_write_reg(client, MPU6050_REG_ACCEL_CONFIG, 0x18);
    if (ret < 0)
        return ret;

    dev_info(&client->dev, "MPU6050 initialized OK\n");
    return 0;
}

//写数据读取函数
static int mpu6050_read_sensors(struct i2c_client *client,
                                struct mpu6050_data *data)
{
    u8 buf[14];   // 14 字节缓冲区
    int ret;

    // 从 0x3B 开始读 14 字节
    ret = i2c_smbus_read_i2c_block_data(client,
                                        MPU6050_REG_ACCEL_XOUT_H,
                                        MPU6050_DATA_SIZE,
                                        buf);
    if (ret != MPU6050_DATA_SIZE) {
        dev_err(&client->dev, "block read failed\n");
        return -EIO;
    }

    // 大端序拼接：高字节左移8位 | 低字节
    data->accel_x = (short)((buf[0]  << 8) | buf[1]);   //
    data->accel_y = (short)((buf[2]  << 8) | buf[3]);
    data->accel_z = (short)((buf[4]  << 8) | buf[5]);
    data->temp    = (short)((buf[6]  << 8) | buf[7]);
    data->gyro_x  = (short)((buf[8]  << 8) | buf[9]);
    data->gyro_y  = (short)((buf[10] << 8) | buf[11]);
    data->gyro_z  = (short)((buf[12] << 8) | buf[13]);

    return 0;
}

//read 回调 - 用户调用 read() 时触发
static ssize_t mpu6050_read(struct file *fp, char __user *ubuf,
                            size_t count, loff_t *offset)
{
    struct mpu6050_data data;
    int ret;

    if (count < sizeof(data))
        return -EINVAL;

    ret = mpu6050_read_sensors(g_client, &data);  // 用全局 g_client
    if (ret < 0)
        return ret;

    if (copy_to_user(ubuf, &data, sizeof(data)))
        return -EFAULT;

    return sizeof(data);
}

static struct file_operations mpu6050_fops = {
    .owner = THIS_MODULE,
    .read  = mpu6050_read,
};

static struct miscdevice mpu6050_misc = {
    .minor = MISC_DYNAMIC_MINOR,   // 内核自动分配设备号
    .name  = "mpu6050_misc",       // /dev/mpu6050_misc
    .fops  = &mpu6050_fops,
};

//内核发现i2C设备时调用
static int mpu6050_probe(struct i2c_client *client, const struct i2c_device_id *id)
{   
    int ret;
    g_client = client;
    ret = mpu6050_chip_init(client);   //芯片初始化
    if (ret < 0)
        return ret;
    ret = misc_register(&mpu6050_misc);
    if (ret < 0) {
        dev_err(&client->dev, "misc_register failed\n");
    return ret;
    }
        dev_info(&client->dev, "MPU6050 probed, addr = 0x%02x\n",client->addr);
    return 0;
}

//设备移除或者rmmod时调用
static int mpu6050_remove(struct i2c_client *client)
{
    misc_deregister(&mpu6050_misc);
    dev_info(&client->dev, "MPU6050 removed");
    return 0;
}

//设备树匹配表
static const struct of_device_id mpu6050_of_match[] = {
MODULE_DEVICE_TABLE(i2c, mpu6050_id);    {.compatible = "invensense,mpu6050"},  // 和设备树一致
    { }  // 空条目 = 哨兵，表示数组结束
};
MODULE_DEVICE_TABLE(of,mpu6050_of_match);// 导出给内核

//传统ID匹配表
static const struct i2c_device_id mpu6050_id[] = {
    {"mpu6050", 0 },
    { }  //哨兵
};


static struct i2c_driver mpu6050_driver = {
    .probe   = mpu6050_probe,
    .remove  = mpu6050_remove,
    .driver  ={
        .name           = "mpu6050",
        .owner          = THIS_MODULE,      // 模块引用计数
        .of_match_table = mpu6050_of_match, //设备树匹配表
    }, 
    .id_table = mpu6050_id                  // 传统ID匹配
};


module_i2c_driver(mpu6050_driver);         //会自动生成 module_init 和 module_exit，把 I2C 驱动注册到内核。
MODULE_LICENSE("GPL");                     //许可证，否则内核会抱怨污染。license:        GPL
MODULE_AUTHOR("pipi");                     //作者名字
MODULE_DESCRIPTION("MPU6050 6-Axis IMU I2C Driver");//模块的描述