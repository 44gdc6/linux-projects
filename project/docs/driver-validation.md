# 驱动验证说明

这份文档描述目标板上的驱动加载、`/dev` 节点检查、最小读写验证以及整机联调路径，方便你在实际调试和面试时复述验证方法。

## 1. 构建

```bash
make drivers
make app
```

确认：

- 所有预期 `.ko` 文件都已生成
- `app/build/demo` 已生成

## 2. 加载模块

在目标板上根据实际依赖顺序加载模块：

```bash
insmod lm75_drv.ko
insmod mq3_drv.ko
insmod adxl345_drv.ko
insmod dht11_drv.ko
insmod beep_drv.ko
```

查看近期内核日志：

```bash
dmesg | tail -n 50
```

预期关键字包括：

- `lm75 probe success`
- `mq3 probe success`
- `adxl345 probe success`
- `dht11 probe success`
- `beep probe success`

## 3. 检查设备节点

```bash
ls -l /dev/lm75_misc /dev/mq3_misc /dev/adxl345_misc /dev/dht11_misc /dev/beep_misc
```

预期：

- 所有节点存在
- 每个节点都是字符设备

## 4. 最小读写验证

### LM75

目标：确认 I2C 路径能读取原始温度值。

预期：

- `read()` 返回 `2`
- 原始值可以转换为合理温度

### MQ3

目标：确认基于 IIO channel 的 misc device 能返回原始 ADC 值。

预期：

- `read()` 返回 `sizeof(int)`
- 原始值会随传感器输入变化

### ADXL345

目标：确认 SPI 通信和三轴数据采集。

预期：

- `read()` 返回 `6`
- `x/y/z` 随姿态或振动变化

### DHT11

目标：确认 GPIO 时序和数据帧读取正常。

预期：

- `read()` 返回 `5`
- 校验和通过
- 用户态能正确解析湿度

失败线索：

- `-ETIMEDOUT`：传感器没有在预期时间内应答
- `-EBADMSG`：采集到的数据帧校验失败

### 蜂鸣器

目标：确认告警输出链路正常。

预期：

- 向 `/dev/beep_misc` 写入时 `write()` 返回 `4`
- 从 `/dev/beep_misc` 读取时 `read()` 返回 `4`
- 硬件状态会随 `BEEP_ON` / `BEEP_OFF` 改变

## 5. 整机应用验证

启动应用：

```bash
./demo
```

观察完整链路：

- 采集线程从 `/dev/*` 读取原始数据
- `collect_thread` 组装统一样本
- 蜂鸣器随告警状态变化
- SQLite 线程收到样本并落库
- MQTT 线程完成上报
- LVGL 线程刷新界面

## 6. 常见失败场景

### 模块未加载

现象：

- `/dev/*` 节点不存在
- 用户态打开节点失败

检查：

```bash
lsmod
dmesg | tail -n 50
```

### 读取失败

现象：

- 用户态 `read()` 返回负值
- 应用日志中出现设备读取失败

排查方向：

- 总线接线是否正常
- 设备树匹配是否正确
- GPIO 编号是否正确
- 传感器供电是否稳定

### 卸载 / 重载

```bash
rmmod beep_drv
insmod beep_drv.ko
```

预期：

- 节点重新创建
- 重载后读写行为仍然正常

## 7. 面试时可直接复述的总结

一句话版本：

> 我会先用 `insmod`、`dmesg` 和 `/dev` 节点检查确认驱动绑定成功，再做最小 userspace 读写验证，最后跑完整应用确认驱动到用户态链路。
