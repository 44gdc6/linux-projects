# 危险品装载与驾驶舱安全监测终端

基于 **NXP i.MX6ULL**（ARM Cortex-A7 + Linux）的综合监测终端，搭配 **STM32F407/F103** CAN 总线节点，实现对危险品运输车辆装载区与驾驶舱的全方位安全监控。

## 系统架构

```
感知层 → Linux驱动与接口层 → 设备抽象层 → 业务逻辑层 → 本地交互层 → 平台通信层
```

```
┌──────────────────────────────────────────────┐
│              平台通信层 (MQTT → OneNET)        │
├──────────────────────────────────────────────┤
│              本地交互层 (LVGL / SQLite)        │
├──────────────────────────────────────────────┤
│              业务逻辑层 (数据采集 / 风险判断)    │
├──────────────────────────────────────────────┤
│              设备抽象层 (统一接口 / 数据标准化)  │
├──────────────────────────────────────────────┤
│              Linux 驱动层 (I²C / SPI / GPIO)   │
├──────────────────────────────────────────────┤
│              感知层 (传感器阵列 + CAN 节点)     │
└──────────────────────────────────────────────┘
```

### 监测区域

| 区域 | 传感器 | 监测项 |
|------|--------|--------|
| 装载区 | MQ-2, DS18B20, 门磁, MPU6050, 烟雾, 火焰 | 可燃气体泄漏、温度异常、非法开门、振动 |
| 驾驶舱 | MQ-3, AHT20, 烟雾, MAX30100, GPS, BMP280 | 酒精检测、温湿度、烟雾、心率、位置 |

## 目录结构

```
├── project/                        # Linux 主应用
│   ├── app/
│   │   ├── src/core/               # 线程调度 (mailbox)、日志
│   │   ├── src/device/             # 传感器抽象层 (sensor_common, cJSON)
│   │   ├── src/comm/               # CAN / MQTT 通信协议
│   │   ├── src/service/            # 业务逻辑 (数据采集、天气)
│   │   ├── src/storage/            # SQLite 本地存储
│   │   ├── src/ui/                 # LVGL 图形界面
│   │   ├── include/                # 头文件 & 配置
│   │   ├── third_party/lvgl/       # LVGL 图形库
│   │   └── third_party/lv_drivers/ # LVGL 显示驱动
│   ├── drivers/                    # Linux 内核驱动 (misc 设备)
│   │   ├── lm75_drv/               # LM75 I²C 温度传感器
│   │   ├── adxl345_drv/            # ADXL345 SPI 加速度计
│   │   ├── dht11_drv/              # DHT11 温湿度 (Platform+GPIO)
│   │   ├── mq3_drv/                # MQ-3 酒精传感器 (IIO ADC)
│   │   └── beep_drv/               # 蜂鸣器 GPIO 控制
│   └── docs/                       # 驱动设计 & 验证文档
│
├── stm32-firmware/                 # STM32 CAN 节点固件
│   ├── F407/                       # STM32F407ZGTx 节点
│   └── UART/                       # STM32F103C8T6 节点
│
├── 传感器驱动/                      # 独立传感器驱动模块 (含 DTS)
├── CAN/                            # CAN 总线入门实验
├── lv_mqtt_SQ_pthread/             # 早期 MQTT + LVGL 原型
├── 天气/                           # HTTP 天气 API 客户端
├── 日志/                           # 分级日志模块
└── 项目开发日志/                    # 开发计划 & 进度跟踪
```

## 主线程序线程模型

```c
main()
  ├── collect_thread    // 传感器数据采集
  ├── weather_thread    // 外部天气获取 (HTTP)
  ├── lvgl_thread       // LCD GUI 渲染
  ├── sqlite_thread     // 本地数据持久化
  ├── mqtt_thread       // 云端数据上报
  └── can_thread        // CAN 总线多节点通信
        ↓
  mailbox (消息队列线程调度器 + 链接队列)
```

## 内核驱动

所有驱动基于 Linux **misc 设备框架**，提供统一的 `/dev/*` 字符设备接口：

| 驱动 | 总线 | 设备节点 | 数据格式 |
|------|------|---------|---------|
| LM75 | I²C | `/dev/lm75_misc` | read() → 2字节原始温度 |
| ADXL345 | SPI | `/dev/adxl345_misc` | read() → 3×short (x/y/z) |
| DHT11 | Platform+GPIO | `/dev/dht11_misc` | read() → 5字节原始帧 |
| MQ-3 | IIO ADC | `/dev/mq3_misc` | read() → int 原始 ADC |
| BEEP | GPIO | `/dev/beep_misc` | read()/write() 开关控制 |

## CAN 总线协议

i.MX6ULL 作为主站，STM32 节点作为从站，500 kbps 标准帧 (11-bit ID)：

| 帧类型 | ID 范围 | 说明 |
|--------|---------|------|
| 心跳帧 | `0x100 + node_id` | 节点周期性上报在线状态 |
| 命令帧 | `0x300 + node_id` | 主站下发 PING/状态读取/参数配置 |
| 应答帧 | `0x200 + node_id` | 节点响应命令结果 |

## 构建

### Linux 主应用

```bash
# 需要 arm-linux-gnueabihf-gcc 交叉编译工具链
cd project
make all          # 编译 app + 所有内核驱动
make clean        # 清理
```

### STM32 固件

使用 **STM32CubeMX** 生成代码，**Keil MDK-ARM V5** 编译：

1. 打开 `stm32-firmware/F407/F407.ioc` 或 `stm32-firmware/UART/UART.ioc`
2. 生成代码
3. 用 Keil 打开对应的 `.uvprojx` 工程，编译下载

### 运行 Python 契约测试

```bash
cd stm32-firmware/F407
python -m unittest tests.test_can_multinode_source_contracts -v

cd stm32-firmware/UART
python -m unittest tests.test_can_source_contracts -v
```

## 依赖

- **交叉编译**: `arm-linux-gnueabihf-gcc`
- **MQTT**: Paho MQTT C Client
- **数据库**: SQLite3
- **加密**: OpenSSL
- **GUI**: LVGL v8
- **MCU**: STM32CubeMX 6.0.1 + Keil MDK-ARM V5 + STM32 HAL

## 许可证

MIT License
