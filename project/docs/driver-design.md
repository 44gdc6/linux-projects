# 驱动设计说明

这份文档用于统一说明每个驱动的设计方式、`/dev` 接口语义以及与用户态的协同关系，方便简历包装和面试讲解。

## 统一设计选择

本项目所有设备都通过 misc device 暴露为 `/dev/*` 节点。

这样设计的原因：

- 用户态调试路径直接，方便 `open/read/write`
- 不需要单独维护复杂的字符设备号分配流程
- 对求职展示型项目来说，能够更直观地体现“驱动到应用”的完整链路

第一轮优化不引入 `ioctl`、poll 或阻塞 I/O 扩展，重点放在硬件绑定、数据搬运和用户态协同上。

## LM75 驱动

- 源码：`drivers/lm75_drv/lm75_drv.c`
- 绑定方式：I2C driver
- 匹配信息：
  - device id：`putelm75`
  - compatible：`pute,putelm75`
- 节点：`/dev/lm75_misc`
- 数据格式：
  - `read()` 返回 2 字节原始温度值
  - 驱动内部完成 LM75 温度寄存器读取和位移整理
- 用户态适配：
  - `app/src/device/sensor_lm75.c`
  - 负责把原始值转换成摄氏温度

面试可讲点：

- I2C probe 和寄存器读取流程
- `copy_to_user` 的使用方式
- 这轮优化后，成功读取返回 `sizeof(tmpval)`，而不是 `0`

## MQ3 驱动

- 源码：`drivers/mq3_drv/mq3_drv.c`
- 绑定方式：platform driver + IIO consumer channel
- 匹配信息：
  - compatible：`pute,putemq3`
- 节点：`/dev/mq3_misc`
- 数据格式：
  - `read()` 返回一个 `int raw_value`
- 用户态适配：
  - `app/src/device/sensor_mq3.c`
  - service 层根据原始值做酒精等级判定

面试可讲点：

- platform 驱动如何结合 IIO consumer channel
- 使用互斥锁保护原始数据访问
- 如何把 ADC 原始值通过 misc device 暴露给用户态

## ADXL345 驱动

- 源码：`drivers/adxl345_drv/adxl345_drv.c`
- 绑定方式：SPI driver
- 匹配信息：
  - device id：`puteadxl345`
  - compatible：`pute,puteadxl345`
- 节点：`/dev/adxl345_misc`
- 数据格式：
  - `read()` 返回 3 个 `short`，分别对应 `x/y/z`
- 用户态适配：
  - `app/src/device/sensor_adxl345.c`

面试可讲点：

- probe 阶段对 `DEVID` 的检查
- `DATA_FORMAT` 和 `POWER_CTL` 的初始化
- 通过 `spi_write_then_read` 一次读取 6 字节加速度数据

## DHT11 驱动

- 源码：`drivers/dht11_drv/dht11_drv.c`
- 绑定方式：platform driver + 设备树 GPIO
- 匹配信息：
  - device id：`putedht11`
  - compatible：`pute,putedht11`
- 节点：`/dev/dht11_misc`
- 数据格式：
  - `read()` 返回 5 字节 DHT11 原始帧
- 用户态适配：
  - `app/src/device/sensor_dht11.c`
  - 用户态负责解析湿度值

面试可讲点：

- GPIO 时序控制的 start / reply / read-data 流程
- 用 `spin_lock_irqsave` 保护时序敏感段
- 这轮优化后使用标准错误码表示超时和校验失败

## 蜂鸣器驱动

- 源码：`drivers/beep_drv/beep_drv.c`
- 绑定方式：设备树 GPIO 查找
- 节点：`/dev/beep_misc`
- 数据格式：
  - `read()` 返回当前蜂鸣器状态 `int`
  - `write()` 接收 `BEEP_ON` / `BEEP_OFF` 对应的 `int`
  - 同时保留一个 sysfs 属性节点用于手动调试
- 用户态适配：
  - `app/src/device/beep_control.c`

面试可讲点：

- GPIO 输出控制如何通过 misc device 对外提供统一接口
- 用户态如何通过 `/dev/beep_misc` 打通告警联动
- 这轮优化补齐了长度检查、标准错误码和规范的返回字节数

## 与用户态的边界

驱动层职责尽量保持简单：

- 绑定硬件资源
- 获取原始数据
- 暴露最小 `/dev/*` 接口契约

用户态负责：

- 数据解析
- 阈值判断
- 酒精 / 运动告警
- SQLite 存储
- MQTT 上报
- LVGL 展示

这个边界对驱动岗面试很重要，因为它能清楚说明：驱动聚焦硬件访问，业务逻辑留在用户态，不混杂。
