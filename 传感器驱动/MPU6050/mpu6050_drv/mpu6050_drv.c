#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <asm/uaccess.h>

/* ================================================================ */
/*  寄存器定义 — 全部来自 MPU-6000/6050 Register Map 数据手册        */
/* ================================================================ */

/* 芯片 ID 验证 */
#define MPU6050_REG_WHO_AM_I      0x75
#define MPU6050_VAL_WHO_AM_I      0x68

/* 电源管理 */
#define MPU6050_REG_PWR_MGMT_1    0x6B

/* 采样率分频 — 输出速率 = 8kHz / (1 + SMPLRT_DIV) */
#define MPU6050_REG_SMPLRT_DIV    0x19

/* 数字低通滤波器 */
#define MPU6050_REG_CONFIG        0x1A

/* 陀螺仪量程配置 */
#define MPU6050_REG_GYRO_CONFIG   0x1B

/* 加速度计量程配置 */
#define MPU6050_REG_ACCEL_CONFIG  0x1C

/* 传感器数据起始地址（从这里开始连续 14 字节） */
#define MPU6050_REG_ACCEL_XOUT_H  0x3B

/* 一次突发读的数据长度 */
#define MPU6050_DATA_SIZE         14

/* ================================================================ */
/*  数据结构 — 打包所有传感器原始值，一次 read() 返回全部            */
/* ================================================================ */
struct mpu6050_data {
    short accel_x;
    short accel_y;
    short accel_z;
    short temp;
    short gyro_x;
    short gyro_y;
    short gyro_z;
};

/* ================================================================ */
/*  全局变量 — probe 时保存 i2c_client，read 时拿出来用              */
/* ================================================================ */
static struct i2c_client *g_client;

/* ================================================================ */
/*  I2C 基础通信层：读/写单个寄存器                                   */
/* ================================================================ */

/*
 * 读一个寄存器（1 字节）
 * 等价于：发 START → 发从机地址(写) → 发寄存器地址 →
 *         REPEATED START → 发从机地址(读) → 收 1 字节 → 发 STOP
 */
static int mpu6050_read_reg(struct i2c_client *client, u8 reg, u8 *val)
{
    int ret;

    ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0) {
        dev_err(&client->dev, "read reg 0x%02x failed: %d\n", reg, ret);
        return ret;
    }
    *val = (u8)ret;
    return 0;
}

/*
 * 写一个寄存器（1 字节）
 * 等价于：发 START → 发从机地址(写) → 发寄存器地址 → 发数据 → 发 STOP
 */
static int mpu6050_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
    int ret;

    ret = i2c_smbus_write_byte_data(client, reg, val);
    if (ret < 0) {
        dev_err(&client->dev, "write reg 0x%02x val 0x%02x failed: %d\n",
                reg, val, ret);
        return ret;
    }
    return 0;
}

/* ================================================================ */
/*  芯片初始化 — probe 的核心，LM75 没有这步                         */
/* ================================================================ */
static int mpu6050_chip_init(struct i2c_client *client)
{
    u8 val;
    int ret;

    /* ① 验证芯片身份 — 读 WHO_AM_I，确认不是假芯片/焊接问题 */
    ret = mpu6050_read_reg(client, MPU6050_REG_WHO_AM_I, &val);
    if (ret < 0)
        return ret;

    if (val != MPU6050_VAL_WHO_AM_I) {
        dev_err(&client->dev,
                "wrong chip ID: 0x%02x (expected 0x%02x)\n",
                val, MPU6050_VAL_WHO_AM_I);
        return -ENODEV;
    }
    dev_info(&client->dev, "MPU6050 chip found (WHO_AM_I = 0x%02x)\n", val);

    /* ② 唤醒芯片 — 上电默认 SLEEP 模式，写 0 唤醒 */
    ret = mpu6050_write_reg(client, MPU6050_REG_PWR_MGMT_1, 0x00);
    if (ret < 0)
        return ret;

    msleep(100);    /* 等待内部晶振起振，数据手册建议 */

    /* ③ 配置采样率分频 — 采样率 = 8kHz / (1+7) = 1kHz */
    ret = mpu6050_write_reg(client, MPU6050_REG_SMPLRT_DIV, 0x07);
    if (ret < 0)
        return ret;

    /* ④ 配置数字低通滤波器 — DLPF=3，带宽约 44Hz */
    ret = mpu6050_write_reg(client, MPU6050_REG_CONFIG, 0x03);
    if (ret < 0)
        return ret;

    /* ⑤ 陀螺仪量程：±2000 °/s */
    ret = mpu6050_write_reg(client, MPU6050_REG_GYRO_CONFIG, 0x18);
    if (ret < 0)
        return ret;

    /* ⑥ 加速度计量程：±16g */
    ret = mpu6050_write_reg(client, MPU6050_REG_ACCEL_CONFIG, 0x18);
    if (ret < 0)
        return ret;

    dev_info(&client->dev, "MPU6050 initialized OK\n");
    return 0;
}

/* ================================================================ */
/*  传感器数据读取 — 从 0x3B 突发读 14 字节，拼接成大端序 short      */
/* ================================================================ */
static int mpu6050_read_sensors(struct i2c_client *client,
                                struct mpu6050_data *data)
{
    u8 buf[MPU6050_DATA_SIZE];
    int ret;

    /*
     * i2c_smbus_read_i2c_block_data 自动完成：
     *   START → addr(W) → 0x3B → REPEATED START → addr(R) → 14 bytes → STOP
     */
    ret = i2c_smbus_read_i2c_block_data(client,
                                        MPU6050_REG_ACCEL_XOUT_H,
                                        MPU6050_DATA_SIZE,
                                        buf);
    if (ret != MPU6050_DATA_SIZE) {
        dev_err(&client->dev, "block read failed: %d\n", ret);
        return -EIO;
    }

    /*
     * MPU6050 数据是大端序（高字节在前），拼接成有符号 short。
     * 寄存器布局：
     *   buf[0:1]  → ACCEL_X   (0x3B, 0x3C)
     *   buf[2:3]  → ACCEL_Y   (0x3D, 0x3E)
     *   buf[4:5]  → ACCEL_Z   (0x3F, 0x40)
     *   buf[6:7]  → TEMP      (0x41, 0x42)
     *   buf[8:9]  → GYRO_X    (0x43, 0x44)
     *   buf[10:11]→ GYRO_Y    (0x45, 0x46)
     *   buf[12:13]→ GYRO_Z    (0x47, 0x48)
     */
    data->accel_x = (short)((buf[0]  << 8) | buf[1]);
    data->accel_y = (short)((buf[2]  << 8) | buf[3]);
    data->accel_z = (short)((buf[4]  << 8) | buf[5]);
    data->temp    = (short)((buf[6]  << 8) | buf[7]);
    data->gyro_x  = (short)((buf[8]  << 8) | buf[9]);
    data->gyro_y  = (short)((buf[10] << 8) | buf[11]);
    data->gyro_z  = (short)((buf[12] << 8) | buf[13]);

    return 0;
}

/* ================================================================ */
/*  文件操作层 — 应用层接口                                          */
/* ================================================================ */
static ssize_t mpu6050_read(struct file *fp, char __user *ubuf,
                            size_t count, loff_t *offset)
{
    struct mpu6050_data data;
    int ret;

    /* 用户缓冲区必须能装下一个完整的 mpu6050_data */
    if (count < sizeof(data))
        return -EINVAL;

    ret = mpu6050_read_sensors(g_client, &data);
    if (ret < 0)
        return ret;

    if (copy_to_user(ubuf, &data, sizeof(data)))
        return -EFAULT;

    return sizeof(data);    /* ← 返回实际读到的字节数 */
}

static struct file_operations mpu6050_fops = {
    .owner = THIS_MODULE,
    .read  = mpu6050_read,
};

/* ================================================================ */
/*  杂项设备 — 自动创建 /dev/mpu6050_misc                            */
/* ================================================================ */
static struct miscdevice mpu6050_misc = {
    .minor = MISC_DYNAMIC_MINOR,      /* 内核自动分配次设备号 */
    .name  = "mpu6050_misc",           /* 设备节点名 */
    .fops  = &mpu6050_fops,
};

/* ================================================================ */
/*  probe / remove — I2C 设备匹配后的入口和出口                       */
/* ================================================================ */
static int mpu6050_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    int ret;

    g_client = client;                /* ① 全局保存，read() 要用 */

    ret = mpu6050_chip_init(client);  /* ② 初始化芯片 */
    if (ret < 0)
        return ret;                   /* 芯片有问题就不注册设备节点 */

    ret = misc_register(&mpu6050_misc);/* ③ 最后才暴露 /dev 节点 */
    if (ret < 0) {
        dev_err(&client->dev, "misc_register failed: %d\n", ret);
        return ret;
    }

    dev_info(&client->dev, "MPU6050 driver loaded\n");
    return 0;
}

static int mpu6050_remove(struct i2c_client *client)
{
    misc_deregister(&mpu6050_misc);
    dev_info(&client->dev, "MPU6050 driver removed\n");
    return 0;
}

/* ================================================================ */
/*  匹配表 + 驱动注册                                                */
/* ================================================================ */

/* 设备树匹配（OF 方式） */
static const struct of_device_id mpu6050_of_match[] = {
    { .compatible = "invensense,mpu6050" },
    { }
};
MODULE_DEVICE_TABLE(of, mpu6050_of_match);

/* 传统 I2C ID 匹配（兜底） */
static const struct i2c_device_id mpu6050_id[] = {
    { "mpu6050", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, mpu6050_id);

/* I2C 驱动对象 */
static struct i2c_driver mpu6050_driver = {
    .probe    = mpu6050_probe,
    .remove   = mpu6050_remove,
    .driver   = {
        .name           = "mpu6050",
        .owner          = THIS_MODULE,
        .of_match_table = mpu6050_of_match,
    },
    .id_table = mpu6050_id,
};

module_i2c_driver(mpu6050_driver);   /* 等价于 module_init + module_exit */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("pute");
MODULE_DESCRIPTION("MPU6050 6-Axis IMU I2C Driver");
