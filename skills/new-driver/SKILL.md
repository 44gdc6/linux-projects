---
name: new-driver
description: 嵌入式 Linux misc 设备驱动开发助手。根据传感器型号自动生成驱动骨架、设备树节点、用户态测试程序和构建文件。支持 I2C/SPI/GPIO/ADC 总线类型。触发词：新建驱动、驱动开发、misc驱动、设备树、driver template
user-invocable: true
allowed-tools: "Read Write Edit Bash Glob Grep"
hooks:
  UserPromptSubmit:
    - hooks:
        - type: command
          command: "echo '[new-driver] 检测到驱动开发请求'"
metadata:
  version: "1.1.0"
---

# 嵌入式驱动开发助手

为本项目快速生成符合规范的 misc 设备驱动模块。

## 使用方式

```
/new-driver lm75          # 生成 LM75 I2C 温度传感器驱动
/new-driver adxl345       # 生成 ADXL345 SPI 加速度计驱动
/new-driver dht11         # 生成 DHT11 温湿度驱动
/new-driver beep          # 生成蜂鸣器 GPIO 驱动
/new-driver custom mydev  # 生成自定义驱动模板
```

## 执行方式

### 方式 1: 使用生成脚本 (推荐)

```bash
bash ${CLAUDE_PLUGIN_ROOT}/scripts/generate.sh <传感器名称> [总线类型]
```

示例:
```bash
bash ${CLAUDE_PLUGIN_ROOT}/scripts/generate.sh lm75 i2c
bash ${CLAUDE_PLUGIN_ROOT}/scripts/generate.sh adxl345 spi
bash ${CLAUDE_PLUGIN_ROOT}/scripts/generate.sh beep gpio
```

### 方式 2: 手动生成

按照下方模板规则生成文件。

## 生成内容

执行后会在 `传感器驱动/` 目录下创建完整驱动模块：

```
传感器驱动/<编号>-<名称>/
├── <名称>_drv/
│   ├── <名称>_drv.c      # 驱动源码 (misc 设备框架)
│   └── Makefile          # 内核模块构建文件
├── <名称>_app/
│   ├── <名称>_test.c     # 用户态测试程序
│   └── Makefile          # 应用构建文件
└── README.md             # 使用说明
```

## 驱动模板规则

### 1. 识别总线类型

根据传感器名称自动推断：

| 传感器 | 总线 | API |
|--------|------|-----|
| LM75, AHT20, BMP280, MPU6050 | I2C | `i2c_smbus_read_byte_data()` |
| ADXL345, SPI Flash | SPI | `spi_write_then_read()` |
| DHT11, 蜂鸣器, LED | GPIO | `gpiod_get_value()` |
| MQ-3, 光敏电阻 | IIO ADC | `iio_channel_get()` |

### 2. 驱动代码结构

```c
// 标准 misc 设备驱动骨架
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>

// 设备特定头文件 (I2C/SPI/GPIO)
#include <linux/i2c.h>      // I2C 设备
// #include <linux/spi/spi.h>  // SPI 设备
// #include <linux/gpio/consumer.h>  // GPIO 设备

#define DEVICE_NAME  "<名称>_misc"

struct <名称>_dev {
    struct miscdevice misc;
    struct device *dev;
    // 总线特定字段
    // struct i2c_client *client;  // I2C
    // struct spi_device *spi;     // SPI
    // struct gpio_desc *gpio;     // GPIO
    spinlock_t lock;
    unsigned char buf[64];
};

static struct <名称>_dev <名称>_device;

// open
static int <名称>_open(struct inode *inode, struct file *file) {
    pr_info("[%s] device opened\n", DEVICE_NAME);
    return 0;
}

// read - 根据总线类型实现
static ssize_t <名称>_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    // I2C 示例:
    // int val = i2c_smbus_read_byte_data(client, reg);
    // if (copy_to_user(buf, &val, sizeof(val))) return -EFAULT;
    // return sizeof(val);
    return 0;
}

// write - 根据总线类型实现
static ssize_t <名称>_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    // 根据需求实现
    return count;
}

static const struct file_operations <名称>_fops = {
    .owner          = THIS_MODULE,
    .open           = <名称>_open,
    .read           = <名称>_read,
    .write          = <名称>_write,
};

static struct miscdevice <名称>_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &<名称>_fops,
};

// 模块初始化
static int __init <名称>_init(void) {
    int ret;
    pr_info("[%s] module loaded\n", DEVICE_NAME);

    ret = misc_register(&<名称>_misc_device);
    if (ret) {
        pr_err("[%s] failed to register misc device\n", DEVICE_NAME);
        return ret;
    }

    pr_info("[%s] device registered as /dev/%s\n", DEVICE_NAME, DEVICE_NAME);
    return 0;
}

// 模块退出
static void __exit <名称>_exit(void) {
    misc_deregister(&<名称>_misc_device);
    pr_info("[%s] module unloaded\n", DEVICE_NAME);
}

module_init(<名称>_init);
module_exit(<名称>_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("<名称> misc driver");
```

### 3. 用户态测试程序模板

```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define DEVICE_PATH "/dev/<名称>_misc"

int main(int argc, char *argv[]) {
    int fd;
    int ret;
    unsigned char buf[64];

    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return -1;
    }

    // 读取测试
    ret = read(fd, buf, sizeof(buf));
    if (ret > 0) {
        printf("Read %d bytes: ", ret);
        for (int i = 0; i < ret; i++) {
            printf("%02x ", buf[i]);
        }
        printf("\n");
    }

    close(fd);
    return 0;
}
```

### 4. Makefile 模板

**驱动 Makefile:**
```makefile
obj-m += <名称>_drv.o

KDIR ?= /home/alientek/linux-imx

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
```

**应用 Makefile:**
```makefile
CC = arm-linux-gnueabihf-gcc
CFLAGS = -Wall -O2

all: <名称>_test

<名称>_test: <名称>_test.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f <名称>_test
```

## 工作流程

1. 用户输入 `/new-driver <传感器名称>`
2. 分析传感器类型和总线接口
3. 在 `传感器驱动/` 下创建目录结构
4. 生成驱动代码、测试程序、构建文件
5. 输出使用说明

## 项目约定

- 驱动统一使用 misc 设备框架
- 设备节点命名: `/dev/<名称>_misc`
- 日志使用 `pr_info/pr_err`
- 错误码使用负数返回值
- 数据缓冲区大小 64 字节
- 交叉编译工具链: `arm-linux-gnueabihf-gcc`

## 示例

```bash
/new-driver lm75
```

将生成:
- `传感器驱动/xx-lm75_i2c/lm75_drv/lm75_drv.c` - I2C 驱动
- `传感器驱动/xx-lm75_i2c/lm75_app/lm75_test.c` - 测试程序
- `传感器驱动/xx-lm75_i2c/README.md` - 使用文档
