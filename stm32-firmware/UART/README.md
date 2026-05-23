# STM32F103 + TJA1050 CAN 节点示例

## 1. 项目简介

这是一个基于 `STM32F103C8T6 + TJA1050` 的最小 CAN 通信节点工程，使用 `STM32CubeMX + HAL + Keil MDK-ARM` 搭建，当前实现了：

- `USART1` 启动日志输出
- `CAN1` 500 kbps 标准帧通信
- 基于节点号的多节点协议雏形
- 心跳帧周期发送
- 命令帧接收与响应
- CAN 错误状态串口可视化
- Python 源码契约测试

这个仓库适合作为下面几类场景的起点：

- `STM32` 与 `i.MX6ULL` 的 CAN 联调
- `TJA1050` 收发器最小系统验证
- 嵌入式驱动/总线通信学习项目
- 面向校招或初级驱动岗的作品集项目

## 2. 当前已实现功能

### 2.1 启动与日志

上电后，固件会通过 `USART1` 输出启动日志，默认串口参数为 `115200 8N1`。

典型日志如下：

```text
BOOT: HAL init done
BOOT: system clock configured
BOOT: GPIO ready
BOOT: USART1 ready
CAN link ready: 500kbps, node=0x01, RX FIFO0 interrupt enabled
BOOT: CAN link init ok
```

当总线异常时，还会输出类似下面的调试信息：

```text
CAN ERROR err=0x00000087 esr=0x00F80007 tsr=0x1C000001 state=2
CAN TX pending timeout err=0x00000087 esr=0x00F80007 tsr=0x1C000001 state=2
```

### 2.2 CAN 节点协议

当前工程固定为 `node_id = 0x01`，采用 `11-bit` 标准帧，波特率 `500 kbps`。

协议 ID 规划如下：

- 心跳帧：`0x100 + node_id`，当前为 `0x101`
- 响应帧：`0x200 + node_id`，当前为 `0x201`
- 命令帧：`0x300 + node_id`，当前为 `0x301`

已实现的命令如下：

- `0x01`：PING
- `0x02`：读取节点状态
- `0x03`：设置心跳周期，最小支持 `100 ms`

### 2.3 心跳帧格式

心跳帧 ID 为 `0x101`，数据长度固定 `8` 字节：

| 字节 | 含义 |
| --- | --- |
| Byte0 | 节点号 |
| Byte1 | 心跳计数低字节 |
| Byte2 | 心跳计数高字节 |
| Byte3 | 最近一次命令码 |
| Byte4 | 最近一次命令状态 |
| Byte5 | 成功发送计数低字节 |
| Byte6 | 成功接收计数低字节 |
| Byte7 | 最近一次 HAL CAN 错误低字节 |

### 2.4 响应帧格式

响应帧 ID 为 `0x201`，数据长度固定 `8` 字节：

| 字节 | 含义 |
| --- | --- |
| Byte0 | 命令码 |
| Byte1 | 序号 seq |
| Byte2 | 状态码 |
| Byte3 | 节点号 |
| Byte4 | 数据 0 |
| Byte5 | 数据 1 |
| Byte6 | 数据 2 |
| Byte7 | 数据 3 |

状态码定义：

- `0x00`：成功
- `0xE0`：不支持的命令
- `0xE1`：参数错误

## 3. 硬件环境

### 3.1 控制器与外设

- MCU：`STM32F103C8T6`
- CAN 控制器：片内 `CAN1`
- CAN 收发器：`TJA1050`
- 串口调试：`USART1`
- 开发方式：`CubeMX + HAL + Keil MDK-ARM`

### 3.2 引脚定义

#### STM32F103

- `PA9`：`USART1_TX`
- `PA10`：`USART1_RX`
- `PA11`：`CAN_RX`
- `PA12`：`CAN_TX`

#### TJA1050 连接建议

- `PA12 -> TJA1050 TXD`
- `PA11 <- TJA1050 RXD`
- `TJA1050 VCC -> 5V`
- `TJA1050 GND -> STM32 GND / Linux 主控 GND`
- `TJA1050 CANH -> 总线 CANH`
- `TJA1050 CANL -> 总线 CANL`

### 3.3 联调注意事项

这类工程最容易卡在硬件层，不在代码层。联调时建议优先确认：

- `TJA1050` 供电是否为 `5V`
- 两端设备是否共地
- `CANH/CANL` 是否接反
- 两端是否具备 `120Ω` 终端电阻
- Linux 主控与 STM32 是否统一为 `500 kbps`

如果串口一直有日志，但 Linux 端始终收不到帧，优先排查硬件接线和总线电气状态。

## 4. 工程目录说明

```text
UART/
├─ Core/
│  ├─ Inc/                # 头文件
│  └─ Src/                # 应用与外设源码
├─ Drivers/               # STM32 HAL / CMSIS
├─ MDK-ARM/               # Keil 工程与构建输出
├─ tests/                 # Python 源码契约测试
├─ docs/                  # 过程性设计与计划文档
├─ UART.ioc               # CubeMX 工程文件
└─ README.md              # 项目说明
```

重点文件：

- `Core/Src/main.c`：系统启动流程、串口启动日志、主循环
- `Core/Src/can.c`：CAN 初始化、协议实现、错误日志
- `Core/Src/usart.c`：`USART1` 初始化
- `Core/Src/stm32f1xx_it.c`：CAN 中断入口
- `tests/test_can_source_contracts.py`：协议源码约束测试

## 5. 编译与烧录

### 5.1 开发环境

- `Keil MDK-ARM V5`
- `STM32CubeMX 6.x`
- `Python 3.x`，用于运行源码测试

### 5.2 编译步骤

1. 用 Keil 打开 `MDK-ARM/UART.uvprojx`
2. 选择目标配置并编译
3. 生成输出文件后烧录到板卡

生成产物通常位于：

- `MDK-ARM/UART/UART.axf`
- `MDK-ARM/UART/UART.hex`

### 5.3 串口观察

烧录完成后，打开串口工具：

- 波特率：`115200`
- 数据位：`8`
- 停止位：`1`
- 校验位：`None`

如果串口没有任何输出，先不要急着改协议，优先排查：

- 串口是否接在 `PA9/PA10`
- USB 转串口模块是否正常
- 板卡是否真正启动了当前固件
- 供电与下载器是否正常

## 6. Linux 端联调方法

如果对端是 Linux，例如 `i.MX6ULL`，可使用 `can-utils` 联调。

### 6.1 配置 CAN 口

```bash
ip link set can0 down
ip link set can0 type can bitrate 500000
ip link set can0 up
ip -details link show can0
```

### 6.2 监听总线

```bash
candump can0
```

如果 STM32 正常运行，应能看到周期性的 `0x101` 心跳帧。

### 6.3 发送测试命令

#### PING

```bash
cansend can0 301#0101000000000000
```

期望收到 `0x201` 响应，表示节点在线。

#### 读取状态

```bash
cansend can0 301#0202000000000000
```

#### 设置心跳周期为 1000 ms

```bash
cansend can0 301#0303E80300000000
```

说明：

- `0x301` 是发给节点 `0x01` 的命令 ID
- 第二字节是 `seq`
- `0x03E8 = 1000`，按低字节在前发送

## 7. 测试

当前仓库包含 Python 源码契约测试，主要用于保证关键配置和协议骨架没有被改坏。

运行方式：

```bash
python -m unittest tests.test_can_source_contracts
```

当前测试会检查：

- `CAN1` 是否仍为 `500 kbps`
- `main.c` 是否仍初始化并驱动 CAN 链路
- 节点协议 ID 是否符合约定
- 是否保留串口可见的错误诊断输出
- 是否实现 `0x01 / 0x02 / 0x03` 三类命令

## 8. 已知限制

- 当前仓库只包含 `node_id = 0x01` 的 F103 节点实现
- 过滤器当前为全接收模式，适合调试，不适合直接上复杂总线
- 目前没有加入 BootLoader、在线升级、参数持久化等能力
- 当前测试以源码契约测试为主，不等价于完整硬件回归测试
- `MDK-ARM/` 目录里仍可能存在本地构建产物，正式发布前建议清理

## 9. 适合开源吗

从当前工程内容看，这个项目是可以开源的，而且很适合作为“嵌入式总线通信入门 + 联调实战”项目公开。

当前具备的开源基础：

- 有明确的硬件目标
- 有可运行的源代码
- 有串口调试日志
- 有协议结构
- 有最基本的测试

当前还建议补齐的内容：

- 根目录 `LICENSE`
- `.gitignore`
- 一张接线图或接线表截图
- 一份实际串口日志 / `candump` 日志截图
- 如需投简历展示，可再补一张系统架构图

## 10. 许可证说明

本仓库建议以仓库根目录 `LICENSE` 为准。

需要注意：

- `Core/` 中由 `STM32CubeMX` 生成并保留版权头的文件，继续保留原有版权声明
- `Drivers/STM32F1xx_HAL_Driver/` 及相关 ST 文件遵循其原始许可证
- `Drivers/CMSIS/` 中不同文件可能包含 `Apache-2.0` 等第三方许可证

也就是说，这个仓库可以公开，但不要删除原厂头部版权说明，也不要把第三方代码错误地全部声明成你个人独占版权。

## 11. 后续可扩展方向

如果你想把这个仓库继续打磨成更像作品集的项目，推荐往下面几个方向补：

- 增加第二个节点示例，例如 `STM32F407`
- 增加 `i.MX6ULL` 主站收发示例
- 增加节点地址配置与广播命令
- 增加更完整的错误码解释
- 增加总线离线恢复、重发统计和节点状态机
- 增加协议文档图示和抓包示例

---

如果你是拿这个项目投驱动岗，这个仓库的亮点不只是“能发 CAN”，而是你把 `串口可视化调试 + 收发协议 + 中断回调 + 总线错误定位 + Linux 对端联调` 这一整条链路做通了。
