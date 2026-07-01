# MPU6050 Linux I2C 驱动开发复习总结

## 一、整体架构

```
┌─────────────────────────────────────────┐
│  用户空间                               │
│  ./mpu6050_app                          │
│  open("/dev/mpu6050_misc") → read()     │
├─────────────────────────────────────────┤
│  内核空间（mpu6050_drv.ko）             │
│                                         │
│  ⑥ miscdevice → /dev/mpu6050_misc      │
│  ⑤ file_operations { .read }           │
│  ④ mpu6050_read_sensors()              │
│  ③ mpu6050_chip_init()                 │
│  ② mpu6050_read_reg / write_reg        │
│  ① i2c_driver + probe / remove         │
├─────────────────────────────────────────┤
│  Linux I2C 子系统                       │
├─────────────────────────────────────────┤
│  硬件：MPU6050（I2C 地址 0x68）        │
└─────────────────────────────────────────┘
```

---

## 二、驱动开发的 7 个阶段

### 阶段 1：驱动骨架（最小可编译模块）

```c
#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>

static int __init mpu6050_init(void) { return 0; }
static void __exit mpu6050_exit(void) { }

module_init(mpu6050_init);
module_exit(mpu6050_exit);
MODULE_LICENSE("GPL");
```

| 概念 | 说明 |
|------|------|
| `__init` | 函数放入 .init.text 段，加载后释放 |
| `__exit` | rmmod 时调用 |
| `MODULE_LICENSE("GPL")` | **必须**，否则内核污染警告 |
| `MODULE_AUTHOR` | 可选，作者信息 |
| `MODULE_DESCRIPTION` | 可选，模块描述 |

---

### 阶段 2：I2C 驱动框架

把 `module_init/module_exit` 替换为 I2C 专用框架：

```c
// probe — 匹配到设备时调用（入参 client 是后续所有通信的句柄）
static int mpu6050_probe(struct i2c_client *client,
                         const struct i2c_device_id *id) { return 0; }

// remove — 设备移除/rmmod 时调用
static int mpu6050_remove(struct i2c_client *client) { return 0; }

// 设备树匹配表 — compatible 必须和设备树一模一样（注意没有空格）
static const struct of_device_id mpu6050_of_match[] = {
    { .compatible = "invensense,mpu6050" },
    { }  // 哨兵，表示结束
};
MODULE_DEVICE_TABLE(of, mpu6050_of_match);

// 传统 ID 匹配表（兜底方案）
static const struct i2c_device_id mpu6050_id[] = {
    { "mpu6050", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, mpu6050_id);

// I2C 驱动对象
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

module_i2c_driver(mpu6050_driver);  // 自动生成 module_init/exit
```

| 概念 | 说明 |
|------|------|
| `i2c_client` | 内核为每个设备分配的结构体，含 `addr`（I2C地址）、`adapter`（总线） |
| `of_device_id` | 设备树匹配，`compatible` 字符串和设备树必须完全一致 |
| `i2c_device_id` | 传统匹配，`MODULE_DEVICE_TABLE` 导出给 depmod |
| `module_i2c_driver()` | 宏，替代手动写 module_init/exit |
| `{ }` 哨兵 | 匹配表以空条目结束，必不可少 |

---

### 阶段 3：寄存器读写（SMBus API）

```c
#define MPU6050_REG_WHO_AM_I    0x75
// ... 其他寄存器地址 ...

static struct i2c_client *g_client;  // 全局保存，probe 存、read 用

// 读单寄存器
static int mpu6050_read_reg(struct i2c_client *client, u8 reg, u8 *val)
{
    int ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0) { dev_err(...); return ret; }
    *val = (u8)ret;
    return 0;
}

// 写单寄存器
static int mpu6050_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
    int ret = i2c_smbus_write_byte_data(client, reg, val);
    if (ret < 0) { dev_err(...); return ret; }
    return 0;
}
```

| 函数 | I2C 总线操作 | 用途 |
|------|-------------|------|
| `i2c_smbus_read_byte_data` | 写寄存器地址 → 读 1 字节 | 读单个寄存器 |
| `i2c_smbus_write_byte_data` | 写寄存器地址 → 写 1 字节 | 写单个寄存器 |
| `i2c_smbus_read_i2c_block_data` | 写起始地址 → 连读 N 字节 | 突发读（后面用） |

| 概念 | 说明 |
|------|------|
| `u8` | `unsigned char`（0~255），寄存器地址和值都用它 |
| `u8 *val`（指针） | 输出参数，读到的值写到调用者的变量里 |
| `g_client` | 全局变量，probe 时存、read 时取（因为时间上是分离的） |
| `dev_err / dev_info` | 带设备信息的日志，比 `pr_info` 多显示总线号和地址 |

---

### 阶段 4：芯片初始化（上电配置序列）

```c
static int mpu6050_chip_init(struct i2c_client *client)
{
    u8 val;
    int ret;

    // ① 验证身份 — 读 WHO_AM_I，必须等于 0x68
    ret = mpu6050_read_reg(client, 0x75, &val);
    if (val != 0x68) return -ENODEV;

    // ② 唤醒 — 写 PWR_MGMT_1 = 0，然后等 100ms
    mpu6050_write_reg(client, 0x6B, 0x00);
    msleep(100);  // 需要 #include <linux/delay.h>

    // ③ 采样率 1kHz — 8kHz / (1+7) = 1kHz
    mpu6050_write_reg(client, 0x19, 0x07);

    // ④ 低通滤波 44Hz — DLPF_CFG = 3
    mpu6050_write_reg(client, 0x1A, 0x03);

    // ⑤ 陀螺 ±2000°/s — FS_SEL = 3
    mpu6050_write_reg(client, 0x1B, 0x18);

    // ⑥ 加速度 ±16g — AFS_SEL = 3
    mpu6050_write_reg(client, 0x1C, 0x18);

    return 0;
}
```

| 寄存器 | 地址 | 写入值 | 含义 |
|--------|------|--------|------|
| WHO_AM_I | 0x75 | 读 | 验证芯片=0x68 |
| PWR_MGMT_1 | 0x6B | 0x00 | 清除 SLEEP 位 |
| SMPLRT_DIV | 0x19 | 0x07 | 8kHz ÷ 8 = 1kHz |
| CONFIG | 0x1A | 0x03 | DLPF_CFG=3，44Hz |
| GYRO_CONFIG | 0x1B | 0x18 | FS_SEL=3，±2000°/s |
| ACCEL_CONFIG | 0x1C | 0x18 | AFS_SEL=3，±16g |

> **关键**：寄存器值来自数据手册，是按位（bit）配置的，不是随便填。

---

### 阶段 5：传感器数据读取

```c
// 数据结构（14 字节，驱动和应用程序各定义一份，必须一致）
struct mpu6050_data {
    short accel_x, accel_y, accel_z;  // 各 2 字节
    short temp;                        // 2 字节
    short gyro_x,  gyro_y,  gyro_z;   // 各 2 字节
};

static int mpu6050_read_sensors(struct i2c_client *client,
                                struct mpu6050_data *data)
{
    u8 buf[14];

    // 突发读：从 0x3B 连续读 14 字节
    i2c_smbus_read_i2c_block_data(client, 0x3B, 14, buf);

    // 大端序拼接：高字节 << 8 | 低字节
    data->accel_x = (short)((buf[0]  << 8) | buf[1]);
    data->accel_y = (short)((buf[2]  << 8) | buf[3]);
    // ... 其他轴同理 ...
    data->gyro_z  = (short)((buf[12] << 8) | buf[13]);
    return 0;
}
```

| 概念 | 说明 |
|------|------|
| 寄存器布局 | 0x3B~0x48 连续 14 字节，ACC_X→ACC_Y→ACC_Z→TEMP→GYR_X→GYR_Y→GYR_Z |
| 大端序 | 高字节在前，`(buf[0] << 8) | buf[1]` 拼接成 16 位有符号数 |
| `(short)` 强制转换 | 传感器输出有符号数（静止时 Z 轴 ≈ +16384 = 1g） |

---

### 阶段 6：暴露 /dev 设备节点

```c
#include <linux/miscdevice.h>   // misc_register
#include <asm/uaccess.h>        // copy_to_user

// read() 回调 — 用户空间 read() 时触发
static ssize_t mpu6050_read(struct file *fp, char __user *ubuf,
                            size_t count, loff_t *offset)
{
    struct mpu6050_data data;

    if (count < sizeof(data))
        return -EINVAL;

    mpu6050_read_sensors(g_client, &data);              // 读传感器

    if (copy_to_user(ubuf, &data, sizeof(data)))        // 拷贝给用户
        return -EFAULT;

    return sizeof(data);                                 // 返回字节数
}

static struct file_operations mpu6050_fops = {
    .owner = THIS_MODULE,
    .read  = mpu6050_read,
};

static struct miscdevice mpu6050_misc = {
    .minor = MISC_DYNAMIC_MINOR,   // 自动分配次设备号
    .name  = "mpu6050_misc",       // 出现在 /dev/ 下的名字
    .fops  = &mpu6050_fops,
};

// probe 里注册
misc_register(&mpu6050_misc);

// remove 里注销
misc_deregister(&mpu6050_misc);
```

| 概念 | 说明 |
|------|------|
| `miscdevice` | 杂项设备框架，自动创建 `/dev/xxx`，不用手动 `register_chrdev` |
| `MISC_DYNAMIC_MINOR` | 内核自动分配设备号，避免冲突 |
| `copy_to_user` | **必须用**，内核不能直接写用户空间地址 |
| `THIS_MODULE` | 防止使用设备时误卸载模块 |
| `-EINVAL` | 参数无效 |
| `-EFAULT` | 内存访问错误 |

---

### 阶段 7：用户空间应用程序

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

// 和驱动里一模一样的结构体
struct mpu6050_data {
    short accel_x, accel_y, accel_z;
    short temp;
    short gyro_x, gyro_y, gyro_z;
};

int main(void)
{
    int fd = open("/dev/mpu6050_misc", O_RDONLY);
    struct mpu6050_data data;

    while (1) {
        read(fd, &data, sizeof(data));
        printf("ACC: %d %d %d\n", data.accel_x, data.accel_y, data.accel_z);
        sleep(1);
    }
    close(fd);
    return 0;
}
```

> 物理量转换公式（留给进阶）：
> - 加速度(g) = raw × 量程 / 32768
> - 陀螺仪(°/s) = raw × 量程 / 32768
> - 温度(°C) = raw / 340 + 36.53

---

## 三、设备树配置

```dts
&i2c1 {                          // 看硬件原理图确定是 i2c 几
    mpu6050@68 {
        compatible = "invensense,mpu6050";  // 和驱动 of_match 一致
        reg = <0x68>;            // I2C 从机地址
    };
};
```

```bash
make dtbs                              # 编译
cp xxx.dtb ~/nfs/rootfs/              # 部署
```

---

## 四、常用错误码速查

| 错误码 | 含义 | 使用场景 |
|--------|------|---------|
| `0` | 成功 | 正常返回 |
| `-ENODEV` | 没有这个设备 | WHO_AM_I 不匹配 |
| `-EINVAL` | 参数无效 | 用户缓冲区太小 |
| `-EIO` | I/O 错误 | I2C 通信失败 |
| `-EFAULT` | 内存访问错误 | copy_to_user 失败 |

---

## 五、完整数据流

```
用户程序                         内核驱动                       硬件
────────                    ──────────────                  ────
read(fd, buf, 14)  ──→  mpu6050_read()
                              │
                         mpu6050_read_sensors(g_client, &data)
                              │
                         i2c_smbus_read_i2c_block_data()
                              │                    ──────────→  I2C START
                              │                    ←──────────  0x3B ~ 0x48 共 14 字节
                              │
                         大端序拼接成 short
                              │
                         copy_to_user(ubuf, &data, 14)
                              │
                    ←──  返回 14
buf 里有数据了
```

---

## 六、关键的"为什么要这么写"

| 写法 | 原因 |
|------|------|
| `g_client` 全局变量 | probe 拿到的 client，read 时还要用，时间上分离 |
| `copy_to_user` | 内核不能直接访问用户空间指针（可能非法） |
| `MODULE_DEVICE_TABLE` | 导出匹配表给 depmod，让系统知道这个驱动匹配什么设备 |
| `{ }` 空哨兵 | 匹配表是数组，内核遍历到 `{ }` 才知道结束了 |
| `(short)` 强制转换 | 传感器数据是有符号数，不转会当成无符号 |
| `module_i2c_driver` | 替代手动 module_init/exit，自动注册到 I2C 总线 |
| 每一步都判断 `ret < 0` | 一步失败后面全无意义，立刻返回避免连锁错误 |
