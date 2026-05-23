# 危险品装载与驾驶舱安全监测终端

> 基于 NXP i.MX6ULL + STM32 的 ARM Linux 综合嵌入式监测系统

## 项目概述

本项目面向危险品运输车辆，构建一套覆盖 **装载区** 和 **驾驶舱** 两条主线的安全监测终端。
硬件上以 **NXP i.MX6ULL**（ARM Cortex-A7）为主控运行嵌入式 Linux，通过 CAN 总线挂载
**STM32F407 / STM32F103** 节点采集传感器数据；软件上通过 LVGL 提供本地交互界面，
MQTT 连接 OneNET 云平台，SQLite 落盘历史记录。

### 监测范围

| 区域 | 传感器 | 监测项 |
|------|--------|--------|
| **装载区** | MQ-2（可燃气体）、DS18B20（温度）、门磁、MPU6050（振动）、烟雾传感器、火焰传感器 | 气体泄漏、温度异常、非法开门、振动、火情 |
| **驾驶舱** | MQ-3（酒精）、AHT20（温湿度）、烟雾传感器、MAX30100（心率）、GPS、BMP280（气压） | 酒精检测、环境异常、驾驶员状态、位置追踪 |

---

## 系统架构

```
┌──────────────────────────────────────────────────────┐
│                   平台通信层                           │
│        MQTT (OneNET) · 状态上报 · 告警推送            │
├──────────────────────────────────────────────────────┤
│                   本地交互层                           │
│        LVGL LCD 显示 · 蜂鸣器 · LED · SQLite 日志     │
├──────────────────────────────────────────────────────┤
│                   业务逻辑层                           │
│        数据采集调度 · 风险判定 · 分级告警              │
├──────────────────────────────────────────────────────┤
│                   设备抽象层                           │
│        统一设备接口 · 数据标准化 · 状态缓存            │
├──────────────────────────────────────────────────────┤
│                Linux 驱动与接口层                      │
│        GPIO · I²C · SPI · UART · ADC · CAN · Ethernet │
├──────────────────────────────────────────────────────┤
│                     感知层                            │
│        传感器阵列  +  STM32 CAN 节点                  │
└──────────────────────────────────────────────────────┘
```

### 硬件拓扑

```
                  ┌─────────────┐
                  │  OneNET 云   │
                  └──────┬──────┘
                         │ MQTT (Ethernet)
                  ┌──────┴──────┐
                  │  i.MX6ULL   │  Linux 主站
                  │  ARM Linux  │  LVGL + SQLite
                  └──────┬──────┘
                         │ CAN 500kbps
          ┌──────────────┼──────────────┐
          │              │              │
    ┌─────┴─────┐  ┌─────┴─────┐  其他节点
    │ STM32F407 │  │ STM32F103 │
    │ node=0x02 │  │ node=0x01 │
    └───────────┘  └───────────┘
```

---

## 目录结构

```
linux-projects/
│
├── project/                           # Linux 主应用工程
│   ├── app/
│   │   ├── src/core/                  # 线程调度 (mailbox) · 日志 · 链表队列
│   │   ├── src/device/                # 传感器抽象层 (sensor_lm75 / adxl345 / dht11 / mq3)
│   │   ├── src/comm/                  # CAN 协议栈 · MQTT 客户端
│   │   ├── src/service/               # 业务逻辑：collect 数据采集 · weather 天气
│   │   ├── src/storage/               # SQLite 封装
│   │   ├── src/ui/                    # LVGL 用户界面 (lvgl_user)
│   │   ├── include/                   # 头文件 (MQTT / OpenSSL / SQLite / cJSON / 项目配置)
│   │   ├── third_party/               # LVGL v8 · LVGL Drivers
│   │   ├── ui/                        # 界面资源 (screens / fonts / images)
│   │   └── tests/                     # 单元测试代码
│   ├── drivers/                       # Linux 内核 misc 设备驱动
│   │   ├── lm75_drv/                  # LM75 I²C 温度传感器
│   │   ├── adxl345_drv/               # ADXL345 SPI 三轴加速度计
│   │   ├── dht11_drv/                 # DHT11 温湿度 (platform + GPIO)
│   │   ├── mq3_drv/                   # MQ-3 酒精传感器 (IIO ADC)
│   │   └── beep_drv/                  # 蜂鸣器 (GPIO)
│   ├── lib/                           # 交叉编译依赖库
│   │   ├── libpaho-mqtt3*.so          # Paho MQTT C 客户端
│   │   ├── libsqlite3.so              # SQLite3
│   │   └── libcrypto.so / libssl.so   # OpenSSL
│   ├── docs/                          # 驱动设计文档 & 验证说明
│   └── Makefile                       # 顶层构建入口
│
├── stm32-firmware/                    # STM32 CAN 节点固件
│   ├── F407/                          # STM32F407ZGTx (Cortex-M4)
│   │   ├── Core/Inc/ & Src/           # HAL 应用层 (can.c / main.c / usart.c / stm32f4xx_it.c)
│   │   ├── Drivers/CMSIS + HAL        # STM32F4 标准库
│   │   ├── MDK-ARM/                   # Keil 工程 (.uvprojx)
│   │   ├── tests/                     # Python 源码契约测试
│   │   └── docs/                      # 设计文档 & 实现计划
│   └── UART/                          # STM32F103C8T6 (Cortex-M3)
│       ├── Core/                      # 同上结构
│       ├── Drivers/                   # STM32F1 标准库
│       ├── MDK-ARM/                   # Keil 工程
│       └── tests/                     # Python 源码契约测试
│
├── CAN/                               # CAN 总线入门实验
│   ├── 01-CAN总线单个设备环回测试/     # 单节点 CAN 环回 (STM32F103)
│   └── 02-CAN总线三个设备互相通信/     # 三节点 CAN 互通信
│
├── 传感器驱动/                         # 独立传感器驱动模块 (含 DTS + 用户态测试程序)
│   ├── 08-beep_misc_drv_dts/          # 蜂鸣器
│   ├── 13-lm75_i2c/                   # LM75
│   ├── 14-adxl345_spi/                # ADXL345
│   ├── 17-platform_dht11/             # DHT11
│   ├── 18-gps/                        # GPS
│   └── MQ-3/                          # MQ-3 酒精传感器
│
├── lv_mqtt_SQ_pthread/                # 早期 MQTT + LVGL + SQLite 原型
├── 天气/                              # HTTP 天气 API 客户端 (api.k780.com)
├── 日志/                              # 独立分级日志模块 (TRACE → FATAL)
├── MQTT/                              # MQTT 基础连接测试
├── 项目开发日志/                       # 开发计划 · 进度跟踪 · 会话日志
└── GPS/                               # GPS 预留
```

---

## 主应用线程模型

```c
main()
  │
  ├── mailbox_init()                    // 初始化线程调度器
  │
  ├── collect_thread()                  // [采集] 轮询传感器字符设备，解析原始数据
  ├── weather_thread()                  // [天气] HTTP GET → cJSON 解析 → 气象数据
  ├── lvgl_thread()                     // [界面] LVGL 定时刷新，渲染 LCD
  ├── sqlite_thread()                   // [存储] 接收数据 → SQLite INSERT / 定期清理
  ├── mqtt_thread()                     // [云端] MQTT connect → publish 状态 → subscribe 指令
  └── can_thread()                      // [总线] CAN 帧收发 → 协议编解码 → 节点管理
         │
         └── mailbox_waitall_thread_end()
```

线程间通过 `mailbox + linkqueue` 实现消息传递，无锁化生产者-消费者模型。

---

## Linux 内核驱动

所有传感器驱动基于 **misc 设备框架**，提供标准的 `/dev/*` 字符设备接口，
用户态通过 `open()` / `read()` / `write()` / `ioctl()` 访问：

| 驱动 | 总线 | 设备节点 | 用户态 read() 返回 |
|------|------|---------|--------------------|
| LM75 | I²C | `/dev/lm75_misc` | 2 字节原始温度值 |
| ADXL345 | SPI | `/dev/adxl345_misc` | 3 × `short`（x, y, z 轴加速度） |
| DHT11 | Platform + GPIO | `/dev/dht11_misc` | 5 字节原始帧（湿度+温度+校验） |
| MQ-3 | IIO ADC | `/dev/mq3_misc` | `int` 原始 ADC 采样值 |
| BEEP | GPIO | `/dev/beep_misc` | write() 非零=开，零=关 |

> 详细设计见 [project/docs/driver-design.md](project/docs/driver-design.md)

---

## CAN 总线协议

**物理层：** CAN 2.0A, 500 kbps, 11-bit 标准帧 ID  
**拓扑：** i.MX6ULL 做主站轮询，STM32F407/F103 做从站响应  
**收发器：** TJA1050

### 帧 ID 分配

| 帧类型 | ID 范围 | 方向 | 说明 |
|--------|---------|------|------|
| 心跳帧 | `0x100 + node_id` | 从→主 | 节点定时发送，带状态字 |
| 命令帧 | `0x300 + node_id` | 主→从 | PING / 读取状态 / 配置参数 |
| 应答帧 | `0x200 + node_id` | 从→主 | 命令执行结果返回 |

### 节点 ID

| 节点 | ID | 平台 |
|------|----|------|
| F103 最小系统 | `0x01` | STM32F103C8T6 |
| F407 扩展节点 | `0x02` | STM32F407ZGTx |

---

## 构建与运行

### 前置依赖

| 组件 | 用途 | 版本/说明 |
|------|------|----------|
| `arm-linux-gnueabihf-gcc` | 交叉编译 Linux 应用 | Linaro / Buildroot 工具链 |
| Linux Kernel (i.MX6ULL) | 编译内核驱动 | 需配置好内核源码树 |
| Paho MQTT C | MQTT 客户端库 | 预编译在 `project/lib/` |
| SQLite3 | 嵌入式数据库 | 预编译在 `project/lib/` |
| OpenSSL | TLS 加密 | 预编译在 `project/lib/` |
| STM32CubeMX 6.0.1 | STM32 HAL 代码生成 | F1 / F4 系列 |
| Keil MDK-ARM V5 | STM32 固件编译 | ARM Compiler 5/6 |

### 编译 Linux 主应用

```bash
cd project
make all          # 编译 app + 全部 5 个内核驱动
make clean        # 清理
```

### 编译 STM32 固件

```bash
# F407 节点
# 1. 打开 stm32-firmware/F407/F407.ioc → Generate Code
# 2. Keil 打开 stm32-firmware/F407/MDK-ARM/F407.uvprojx → Build

# F103 节点
# 1. 打开 stm32-firmware/UART/UART.ioc → Generate Code
# 2. Keil 打开 stm32-firmware/UART/MDK-ARM/UART.uvprojx → Build
```

### 运行 Python 契约测试

源码级别的协议一致性验证，不依赖硬件：

```bash
cd stm32-firmware/F407
python -m unittest tests.test_can_multinode_source_contracts -v

cd stm32-firmware/UART
python -m unittest tests.test_can_source_contracts -v
```

---

## 相关文档

- [系统设计说明](危险品装载与驾驶舱安全监测终端项目说明.md)
- [驱动设计文档](project/docs/driver-design.md)
- [驱动验证说明](project/docs/driver-validation.md)
- [CAN 多节点方案设计](stm32-firmware/F407/docs/superpowers/specs/2026-05-16-imx6ull-can-multinode-design.md)
- [项目进度日志](项目开发日志/progress.md)

## 许可证

MIT License
