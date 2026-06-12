#!/bin/bash
# new-driver 生成脚本
# 用法: ./generate.sh <传感器名称> [总线类型]

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
DRIVERS_DIR="$PROJECT_ROOT/传感器驱动"

# 参数检查
if [ $# -lt 1 ]; then
    echo -e "${RED}用法: $0 <传感器名称> [总线类型]${NC}"
    echo "总线类型: i2c, spi, gpio, iio (可选，默认自动检测)"
    echo "示例: $0 lm75 i2c"
    exit 1
fi

SENSOR_NAME="$1"
BUS_TYPE="${2:-auto}"

# 自动检测总线类型
detect_bus_type() {
    local name=$(echo "$1" | tr '[:upper:]' '[:lower:]')
    case "$name" in
        lm75|aht20|bmp280|mpu6050|bh1750|ssd1306)
            echo "i2c"
            ;;
        adxl345|spi_flash|w25q*)
            echo "spi"
            ;;
        dht11|beep|led|relay|key)
            echo "gpio"
            ;;
        mq-3|mq3|photoresistor|adc_*)
            echo "iio"
            ;;
        *)
            echo "unknown"
            ;;
    esac
}

# 检测总线类型
if [ "$BUS_TYPE" = "auto" ]; then
    BUS_TYPE=$(detect_bus_type "$SENSOR_NAME")
    if [ "$BUS_TYPE" = "unknown" ]; then
        echo -e "${YELLOW}无法自动检测总线类型，请手动指定${NC}"
        exit 1
    fi
    echo -e "${GREEN}自动检测总线类型: $BUS_TYPE${NC}"
fi

# 生成编号
generate_number() {
    local existing=$(ls -d "$DRIVERS_DIR"/[0-9]*-"$SENSOR_NAME"* 2>/dev/null | wc -l)
    if [ "$existing" -eq 0 ]; then
        echo "01"
    else
        local last=$(ls -d "$DRIVERS_DIR"/[0-9]*-"$SENSOR_NAME"* 2>/dev/null | sort -n | tail -1 | sed 's/.*\/\([0-9]*\).*/\1/')
        printf "%02d" $((last + 1))
    fi
}

NUMBER=$(generate_number)
DIR_NAME="${NUMBER}-${SENSOR_NAME}_${BUS_TYPE}"
TARGET_DIR="$DRIVERS_DIR/$DIR_NAME"

# 检查目录是否已存在
if [ -d "$TARGET_DIR" ]; then
    echo -e "${RED}错误: 目录已存在 $TARGET_DIR${NC}"
    exit 1
fi

echo -e "${GREEN}创建驱动模块: $TARGET_DIR${NC}"

# 创建目录结构
mkdir -p "$TARGET_DIR/${SENSOR_NAME}_drv"
mkdir -p "$TARGET_DIR/${SENSOR_NAME}_app"

# 生成驱动源码
generate_driver() {
    local header=""
    local bus_struct=""
    local bus_init=""
    local bus_read=""
    local bus_write=""
    local bus_exit=""

    case "$BUS_TYPE" in
        i2c)
            header="#include <linux/i2c.h>"
            bus_struct="struct i2c_client *client;"
            bus_init="
    // I2C 设备初始化
    client = container_of(dev, struct i2c_client, dev);
    pr_info(\"[%s] I2C device probed: %s\\n\", DEVICE_NAME, dev_name(&client->dev));
"
            bus_read="
    // I2C 读取示例
    // int val = i2c_smbus_read_byte_data(client, 0x00);
    // if (val < 0) return val;
    // if (copy_to_user(buf, &val, sizeof(val))) return -EFAULT;
    // return sizeof(val);
    return 0;
"
            bus_write="
    // I2C 写入示例
    // u8 data;
    // if (copy_from_user(&data, buf, sizeof(data))) return -EFAULT;
    // i2c_smbus_write_byte_data(client, 0x00, data);
    return count;
"
            ;;
        spi)
            header="#include <linux/spi/spi.h>"
            bus_struct="struct spi_device *spi;"
            bus_init="
    // SPI 设备初始化
    spi = container_of(dev, struct spi_device, dev);
    pr_info(\"[%s] SPI device probed: %s\\n\", DEVICE_NAME, dev_name(&spi->dev));
"
            bus_read="
    // SPI 读取示例
    // u8 tx_buf[1] = {0x00};
    // u8 rx_buf[6];
    // struct spi_transfer t = {
    //     .tx_buf = tx_buf,
    //     .rx_buf = rx_buf,
    //     .len = sizeof(rx_buf),
    // };
    // struct spi_message m;
    // spi_message_init(&m);
    // spi_message_add_tail(&t, &m);
    // spi_sync(spi, &m);
    // if (copy_to_user(buf, rx_buf, sizeof(rx_buf))) return -EFAULT;
    // return sizeof(rx_buf);
    return 0;
"
            bus_write="
    // SPI 写入示例
    // u8 data;
    // if (copy_from_user(&data, buf, sizeof(data))) return -EFAULT;
    // spi_write(spi, &data, sizeof(data));
    return count;
"
            ;;
        gpio)
            header="#include <linux/gpio/consumer.h>"
            bus_struct="struct gpio_desc *gpio;"
            bus_init="
    // GPIO 设备初始化
    gpio = devm_gpiod_get(dev, \"sensor\", GPIOD_OUT_LOW);
    if (IS_ERR(gpio)) {
        pr_err(\"[%s] Failed to get GPIO\\n\", DEVICE_NAME);
        return PTR_ERR(gpio);
    }
"
            bus_read="
    // GPIO 读取示例
    // int val = gpiod_get_value(gpio);
    // if (copy_to_user(buf, &val, sizeof(val))) return -EFAULT;
    // return sizeof(val);
    return 0;
"
            bus_write="
    // GPIO 写入示例
    // u8 data;
    // if (copy_from_user(&data, buf, sizeof(data))) return -EFAULT;
    // gpiod_set_value(gpio, data ? 1 : 0);
    return count;
"
            ;;
        iio)
            header="#include <linux/iio/iio.h>"
            bus_struct="struct iio_dev *indio_dev;"
            bus_init="
    // IIO 设备初始化
    indio_dev = devm_iio_device_alloc(dev, sizeof(struct ${SENSOR_NAME}_data));
    if (!indio_dev) {
        pr_err(\"[%s] Failed to allocate IIO device\\n\", DEVICE_NAME);
        return -ENOMEM;
    }
"
            bus_read="
    // IIO ADC 读取示例
    // struct iio_channel *channels;
    // int val, ret;
    // ret = iio_channel_get(dev, \"voltage0\", &channels);
    // if (ret) return ret;
    // ret = iio_read_channel_raw(&channels[0], &val);
    // iio_channel_release(channels);
    // if (ret < 0) return ret;
    // if (copy_to_user(buf, &val, sizeof(val))) return -EFAULT;
    // return sizeof(val);
    return 0;
"
            bus_write="
    // IIO 写入示例
    return count;
"
            ;;
    esac

    cat > "$TARGET_DIR/${SENSOR_NAME}_drv/${SENSOR_NAME}_drv.c" << EOF
// SPDX-License-Identifier: GPL-2.0
/*
 * ${SENSOR_NAME} sensor driver (misc device)
 * Bus: ${BUS_TYPE}
 */

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
${header}

#define DEVICE_NAME  "${SENSOR_NAME}_misc"
#define CLASS_NAME   "${SENSOR_NAME}_class"

struct ${SENSOR_NAME}_dev {
    struct miscdevice misc;
    struct device *dev;
    ${bus_struct}
    spinlock_t lock;
    unsigned char buf[64];
};

static struct ${SENSOR_NAME}_dev ${SENSOR_NAME}_device;

static int ${SENSOR_NAME}_open(struct inode *inode, struct file *file) {
    pr_info("[%s] device opened\\n", DEVICE_NAME);
    return 0;
}

static ssize_t ${SENSOR_NAME}_read(struct file *file, char __user *buf, size_t count, loff_t *ppos) {
    struct ${SENSOR_NAME}_dev *dev = container_of(file->private_data, struct ${SENSOR_NAME}_dev, misc);
    unsigned long flags;
    ssize_t ret = 0;

    spin_lock_irqsave(&dev->lock, flags);
${bus_read}
    spin_unlock_irqrestore(&dev->lock, flags);

    return ret;
}

static ssize_t ${SENSOR_NAME}_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    struct ${SENSOR_NAME}_dev *dev = container_of(file->private_data, struct ${SENSOR_NAME}_dev, misc);
    unsigned long flags;

    spin_lock_irqsave(&dev->lock, flags);
${bus_write}
    spin_unlock_irqrestore(&dev->lock, flags);

    return count;
}

static const struct file_operations ${SENSOR_NAME}_fops = {
    .owner          = THIS_MODULE,
    .open           = ${SENSOR_NAME}_open,
    .read           = ${SENSOR_NAME}_read,
    .write          = ${SENSOR_NAME}_write,
};

static struct miscdevice ${SENSOR_NAME}_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DEVICE_NAME,
    .fops  = &${SENSOR_NAME}_fops,
};

static int __init ${SENSOR_NAME}_init(void) {
    int ret;

    pr_info("[%s] module loaded\\n", DEVICE_NAME);

    ret = misc_register(&${SENSOR_NAME}_misc_device);
    if (ret) {
        pr_err("[%s] failed to register misc device\\n", DEVICE_NAME);
        return ret;
    }

    pr_info("[%s] device registered as /dev/%s\\n", DEVICE_NAME, DEVICE_NAME);
    return 0;
}

static void __exit ${SENSOR_NAME}_exit(void) {
    misc_deregister(&${SENSOR_NAME}_misc_device);
    pr_info("[%s] module unloaded\\n", DEVICE_NAME);
}

module_init(${SENSOR_NAME}_init);
module_exit(${SENSOR_NAME}_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("${SENSOR_NAME} ${BUS_TYPE} misc driver");
MODULE_VERSION("1.0");
EOF
}

# 生成 Makefile
generate_makefile() {
    cat > "$TARGET_DIR/${SENSOR_NAME}_drv/Makefile" << EOF
obj-m += ${SENSOR_NAME}_drv.o

KDIR ?= /home/alientek/linux-imx

all:
	\$(MAKE) -C \$(KDIR) M=\$(PWD) modules

clean:
	\$(MAKE) -C \$(KDIR) M=\$(PWD) clean
EOF
}

# 生成测试程序
generate_test_app() {
    local read_code=""
    local format_code=""

    case "$BUS_TYPE" in
        i2c|spi)
            read_code="
    // 读取 2 字节数据 (根据传感器调整)
    unsigned short data;
    ret = read(fd, &data, sizeof(data));
    if (ret == sizeof(data)) {
        printf(\"Read: 0x%04X (%d)\\n\", data, data);
    }
"
            ;;
        gpio)
            read_code="
    int val;
    ret = read(fd, &val, sizeof(val));
    if (ret == sizeof(val)) {
        printf(\"GPIO Value: %d\\n\", val);
    }
"
            ;;
        iio)
            read_code="
    int adc_val;
    ret = read(fd, &adc_val, sizeof(adc_val));
    if (ret == sizeof(adc_val)) {
        printf(\"ADC Value: %d\\n\", adc_val);
    }
"
            ;;
    esac

    cat > "$TARGET_DIR/${SENSOR_NAME}_app/${SENSOR_NAME}_test.c" << EOF
/*
 * ${SENSOR_NAME} sensor test application
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define DEVICE_PATH "/dev/${SENSOR_NAME}_misc"

int main(int argc, char *argv[]) {
    int fd;
    int ret;
    int count = 10;

    if (argc > 1) {
        count = atoi(argv[1]);
    }

    printf("${SENSOR_NAME} sensor test (count=%d)\\n", count);
    printf("Opening device: %s\\n", DEVICE_PATH);

    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        printf("Make sure driver is loaded: insmod ${SENSOR_NAME}_drv.ko\\n");
        return -1;
    }

    for (int i = 0; i < count; i++) {
        printf("--- Read %d ---\\n", i + 1);
${read_code}
        usleep(500000);  // 500ms
    }

    close(fd);
    printf("Test completed\\n");
    return 0;
}
EOF

    cat > "$TARGET_DIR/${SENSOR_NAME}_app/Makefile" << EOF
CC = arm-linux-gnueabihf-gcc
CFLAGS = -Wall -O2

all: ${SENSOR_NAME}_test

${SENSOR_NAME}_test: ${SENSOR_NAME}_test.c
	\$(CC) \$(CFLAGS) -o \$@ \$<

clean:
	rm -f ${SENSOR_NAME}_test
EOF
}

# 生成 README
generate_readme() {
    cat > "$TARGET_DIR/README.md" << EOF
# ${SENSOR_NAME} 传感器驱动

## 概述

- 传感器: ${SENSOR_NAME}
- 总线类型: ${BUS_TYPE}
- 设备节点: \`/dev/${SENSOR_NAME}_misc\`

## 编译

### 编译驱动

\`\`\`bash
cd ${SENSOR_NAME}_drv
make
\`\`\`

### 编译测试程序

\`\`\`bash
cd ${SENSOR_NAME}_app
make
\`\`\`

## 运行

### 加载驱动

\`\`\`bash
insmod ${SENSOR_NAME}_drv.ko
\`\`\`

### 运行测试

\`\`\`bash
./${SENSOR_NAME}_test
\`\`\`

### 卸载驱动

\`\`\`bash
rmmod ${SENSOR_NAME}_drv
\`\`\`

## 设备树配置

参考 \`imx6ull-alientek-emmc.dts\` 添加设备节点。
EOF
}

# 执行生成
generate_driver
generate_makefile
generate_test_app
generate_readme

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}驱动模块生成完成!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "目录结构:"
echo -e "  $TARGET_DIR/"
echo -e "  ├── ${SENSOR_NAME}_drv/"
echo -e "  │   ├── ${SENSOR_NAME}_drv.c"
echo -e "  │   └── Makefile"
echo -e "  ├── ${SENSOR_NAME}_app/"
echo -e "  │   ├── ${SENSOR_NAME}_test.c"
echo -e "  │   └── Makefile"
echo -e "  └── README.md"
echo ""
echo -e "下一步:"
echo -e "  1. 检查生成的驱动代码，根据实际传感器调整"
echo -e "  2. 编译驱动: cd ${TARGET_DIR}/${SENSOR_NAME}_drv && make"
echo -e "  3. 加载驱动: insmod ${SENSOR_NAME}_drv.ko"
echo -e "  4. 运行测试: ./${SENSOR_NAME}_test"
