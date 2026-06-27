# 进度日志

## 会话：2026-06-27 (2)

### 功能优化：IWDG 看门狗 + GPS 定位电子围栏

- **状态：** complete
- 执行的操作：
  - **IWDG 硬件看门狗：** 为 F407 (5秒超时) 和 F103 (8秒超时) 启用独立看门狗
  - **GPS 定位：** 移植 ATGM336H NMEA 解析到项目线程架构，新建 gps_thread
  - **电子围栏：** 实现 Haversine 公式球面距离计算，默认西安 50km 围栏
  - **MQTT 扩展：** 上报 GPS 经纬度、海拔、速度、卫星数、围栏状态
- 创建/修改的文件：
  - `stm32-firmware/F407/Core/Inc/stm32f4xx_hal_conf.h` (启用 HAL_IWDG_MODULE_ENABLED)
  - `stm32-firmware/UART/Core/Inc/stm32f1xx_hal_conf.h` (启用 HAL_IWDG_MODULE_ENABLED)
  - `stm32-firmware/F407/Core/Inc/iwdg.h` (新建)
  - `stm32-firmware/F407/Core/Src/iwdg.c` (新建, LSI=32kHz, /128, reload=1250 → 5.0s)
  - `stm32-firmware/UART/Core/Inc/iwdg.h` (新建)
  - `stm32-firmware/UART/Core/Src/iwdg.c` (新建, LSI=40kHz, /128, reload=2500 → 8.0s)
  - `stm32-firmware/F407/Core/Src/main.c` (IWDG init + refresh)
  - `stm32-firmware/UART/Core/Src/main.c` (IWDG init + refresh)
  - `project/app/include/device/sensor_gps.h` (新建, GPS 数据结构)
  - `project/app/src/device/sensor_gps.c` (新建, NMEA $GPRMC/$GPGGA 解析)
  - `project/app/include/service/geofence.h` (新建, 围栏 API)
  - `project/app/src/service/geofence.c` (新建, Haversine 距离算法)
  - `project/app/include/service/gps_service.h` (新建, gps_thread 声明)
  - `project/app/src/service/gps_service.c` (新建, GPS 线程 + 围栏检测 + mailbox)
  - `project/app/include/core/linkqueue.h` (data_t 新增 GPS 字段)
  - `project/app/include/config/app_config.h` (GPS/围栏/MQTT key 配置)
  - `project/app/src/comm/mqtt.c` (上报 GPS 数据)
  - `project/app/src/core/main.c` (注册 gps_thread)
  - `project/app/Makefile` (添加 sensor_gps.c, geofence.c, gps_service.c)

### IWDG 参数

| MCU | LSI 频率 | 预分频 | 重装载 | 超时 | 主循环喂狗周期 |
|-----|---------|--------|--------|------|--------------|
| F407 | 32 kHz | /128 | 1250 | 5.0s | ~10ms |
| F103 | 40 kHz | /128 | 2500 | 8.0s | ~10ms |

## 会话：2026-06-27

### 阶段 9：CAN 传感器节点集成 - F407 DHT11 + F103 火焰传感器

- **状态：** complete
- **开始时间：** 2026-06-27
- 执行的操作：
  - **CAN 协议扩展：** 新增传感器数据帧类型 (ID = 0x400 + node_id)，保持原有心跳/应答/命令帧兼容
  - **F407 DHT11 驱动：** 基于 SysTick/DWT 微秒延迟实现 DHT11 单总线协议 (PB6)，每 2 秒读取温湿度
  - **F103 火焰传感器驱动：** 数字输入读取 (PB5)，低电平有效，每 2 秒上报状态
  - **i.MX6ULL CAN 接收：** 补齐缺失的 `can_node.h`，解析传感器数据帧并通过 mailbox 转发
  - **MQTT 上传：** 扩展 JSON payload，新增 `dht11hum`、`dht11temp`、`flamest` 属性
  - **线程集成：** 在 `main.c` 注册 `can_thread`，修复 CAN 子系统未接入应用的断点
- 创建/修改的文件：
  - `stm32-firmware/F407/Core/Inc/dht11.h` (新建)
  - `stm32-firmware/F407/Core/Src/dht11.c` (新建，~150 行)
  - `stm32-firmware/F407/Core/Src/can.c` (添加 DHT11 传感器数据发送)
  - `stm32-firmware/F407/Core/Src/gpio.c` (启用 GPIOB 时钟)
  - `stm32-firmware/F407/Core/Src/main.c` (添加 DHT11 头文件)
  - `stm32-firmware/UART/Core/Inc/flame.h` (新建)
  - `stm32-firmware/UART/Core/Src/flame.c` (新建，~40 行)
  - `stm32-firmware/UART/Core/Src/can.c` (添加火焰传感器数据发送)
  - `stm32-firmware/UART/Core/Src/gpio.c` (启用 GPIOB 时钟)
  - `stm32-firmware/UART/Core/Src/main.c` (添加 flame 头文件)
  - `project/app/include/comm/can_node.h` (新建，修复编译断点)
  - `project/app/include/core/linkqueue.h` (data_t 新增 CAN 传感器字段)
  - `project/app/include/config/app_config.h` (新增 MQTT key 定义)
  - `project/app/src/comm/can_node.c` (解析传感器帧 + mailbox 转发)
  - `project/app/src/comm/mqtt.c` (发布火焰/DHT11 数据)
  - `project/app/src/core/main.c` (注册 can_thread)
  - `project/app/Makefile` (添加 can_node.c 编译)
  - `项目开发日志/progress.md` (本条)

### CAN 传感器数据帧协议 (V1.1)

| 帧类型 | ID 公式 | F103 ID | F407 ID |
|--------|---------|---------|---------|
| 心跳 | `0x100 + node_id` | `0x101` | `0x102` |
| 应答 | `0x200 + node_id` | `0x201` | `0x202` |
| 命令 | `0x300 + node_id` | `0x301` | `0x302` |
| **传感器数据** | **`0x400 + node_id`** | **`0x401`** | **`0x402`** |

**F407 DHT11 帧 (0x402)：**

| 字节 | 内容 |
|------|------|
| 0 | 湿度整数 (如 65) |
| 1 | 湿度小数 (DHT11 为 0) |
| 2 | 温度整数 (如 25) |
| 3 | 温度小数 (DHT11 为 0) |
| 4 | 有效标志 (0x01=有效, 0x00=无效) |
| 5-7 | 保留 |

**F103 火焰传感器帧 (0x401)：**

| 字节 | 内容 |
|------|------|
| 0 | 火焰状态 (0x01=检测到火焰, 0x00=安全) |
| 1-7 | 保留 |

### 硬件连接

| MCU | 传感器 | GPIO 引脚 | 说明 |
|-----|--------|----------|------|
| STM32F407 | DHT11 | PB6 | 数据线，开漏输出+上拉 |
| STM32F103 | 火焰传感器 | PB5 | 数字输入，上拉，低电平有效 |

## 会话：2026-06-14

### 代码质量修复
- **状态：** complete
- 执行的操作：
  - 修复 LM75 驱动：copy_to_user 返回 -EFAULT、I2C 传输错误检查
  - 修复蜂鸣器驱动：copy_to_user/from_user 返回 -EFAULT、参数检查
  - 修复 MQTT：sprintf → snprintf、printf → LOG_xxx
  - 修复 LVGL：sprintf → snprintf
  - 修复 mailbox：strcpy → strncpy + NULL 截断
  - 修复 linkqueue：quit_linkqueue 添加空队列检查
  - 更新 .gitignore 排除面试文件
- 修改的文件：
  - `project/drivers/lm75_drv/lm75_drv.c`
  - `project/drivers/beep_drv/beep_drv.c`
  - `project/app/src/comm/mqtt.c`
  - `project/app/src/ui/lvgl_user.c`
  - `project/app/src/core/mailbox.c`
  - `project/app/src/core/linkqueue.c`
  - `.gitignore`

## 会话：2026-06-12

### 项目迭代与面试准备
- **状态：** complete
- 执行的操作：
  - 创建项目专属 Skills: `/new-driver` 和 `/can-debug`
  - 添加驱动生成脚本和 CAN 调试脚本
  - 清理 `13-lm75_i2c` 目录编译产物
  - 整理面试题库：115 道驱动工程师面试题
  - 更新 README 添加 Skills 使用说明
  - 提交 git
- 创建/修改的文件：
  - `skills/new-driver/SKILL.md`
  - `skills/new-driver/scripts/generate.sh`
  - `skills/can-debug/SKILL.md`
  - `skills/can-debug/scripts/parse_candump.py`
  - `skills/can-debug/scripts/verify_can.py`
  - `README.md`

## 会话：2026-05-24

### 阶段 7：仓库整合与 GitHub 发布
- **状态：** complete
- **开始时间：** 2026-05-24
- 执行的操作：
  - 将 `CubemxProject`（F407 + UART）整合到 `linux_projects/stm32-firmware/`，统一为一个仓库
  - 创建 `.gitignore`，排除编译产物（`*.exe` `*.ko` `*.o` `*.axf` `*.hex` 等）
  - 清理已误提交的编译产物：`.exe` 测试二进制、内核 `.ko`/`.mod.c`、SSL man 手册、预编译库测试程序
  - 补齐之前遗漏的源文件：MQTT/OpenSSL 头文件、LVGL v8 三方库源码、内核驱动源文件、UI 资源
  - 配置 git 用户信息 (`44gdc6` / `wangshaopi@outlook.com`)
  - 配置 git 代理 `127.0.0.1:7890` 解决 GitHub 直连问题
  - 推送至 [https://github.com/44gdc6/linux-projects](https://github.com/44gdc6/linux-projects)
- 创建/修改的文件：
  - `.gitignore`
  - `README.md`（完全重写）
  - `stm32-firmware/F407/`（从 CubemxProject 迁移）
  - `stm32-firmware/UART/`（从 CubemxProject 迁移）
  - `project/app/include/` 下大量头文件（补追踪）
  - `project/app/third_party/lvgl/`（补追踪）
  - `project/app/third_party/lv_drivers/`（补追踪）

### 阶段 8：安装 Superpowers 技能系统
- **状态：** complete
- 执行的操作：
  - 通过 `npx superpowers-zh` 在用户级 `~/.claude/skills/` 安装 20 个 AI 编程技能
  - 包含：brainstorming、writing-plans、executing-plans、test-driven-development、systematic-debugging、subagent-driven-development、code-review 等
- 创建/修改的文件：
  - `~/.claude/skills/` (20 个 skill 目录)
  - `~/.claude/CLAUDE.md`

## 测试结果（追加）
| 测试 | 输入 | 预期结果 | 实际结果 | 状态 |
|------|------|---------|---------|------|
| Git push 到 GitHub | `git push origin main` | 推送成功，远程可见 | [https://github.com/44gdc6/linux-projects](https://github.com/44gdc6/linux-projects) 在线可访问 | 通过 |

## 会话：2026-05-16

### 阶段 1：现状梳理
- **状态：** complete
- **开始时间：** 2026-05-16
- 执行的操作：
  - 检查 `UART` 工程中 F103 现有 CAN 实现与测试文件
  - 检查 `F407` 工程中的 `main.c`、`can.c`、`usart.c`、`stm32f4xx_it.c`
  - 确认 `F407` 当前没有正式设计文档与实现计划
- 创建/修改的文件：
  - 无

### 阶段 2：规划文档落地
- **状态：** complete
- 执行的操作：
  - 创建多节点任务跟踪文件
  - 编写设计说明
  - 编写实现计划
  - 将方案 3 固化为“统一协议模板 + 节点 ID 区分 + 主站轮询”
- 创建/修改的文件：
  - `task_plan.md`
  - `findings.md`
  - `progress.md`
  - `docs/superpowers/specs/2026-05-16-imx6ull-can-multinode-design.md`
  - `docs/superpowers/plans/2026-05-16-imx6ull-can-multinode.md`

### 阶段 3：F407 节点实现
- **状态：** complete
- 执行的操作：
  - 新建 `tests/test_can_multinode_source_contracts.py`
  - 先运行失败测试，确认 `CAN_Link_Init()`、中断入口和多节点协议宏确实缺失
  - 在 `F407` 工程中补齐 `500 kbps` 时序、心跳、命令、应答、错误日志和 `CAN1_SCE` 中断
  - 运行 `python -m unittest tests.test_can_multinode_source_contracts -v`
  - 运行 Keil 构建并确认 `F407.axf` / `F407.htm` 更新时间
- 创建/修改的文件：
  - `F407/Core/Inc/can.h`
  - `F407/Core/Src/can.c`
  - `F407/Core/Src/main.c`
  - `F407/Core/Src/stm32f4xx_it.c`
  - `F407/tests/__init__.py`
  - `F407/tests/test_can_multinode_source_contracts.py`

### 阶段 4：F103 协议对齐
- **状态：** complete
- 执行的操作：
  - 更新 `UART` 工程测试期望，从 `0x555` 单节点演示切到多节点协议
  - 将 `UART/Core/Src/can.c` 改为统一节点协议，节点 ID 固定为 `0x01`
  - 补 `CAN1_SCE_IRQHandler()`，让错误中断路径闭环
  - 运行 `python -m unittest tests.test_can_source_contracts -v`
  - 运行 Keil 构建并确认 `UART.axf` / `UART.htm` 更新时间
- 创建/修改的文件：
  - `UART/Core/Src/can.c`
  - `UART/Core/Inc/stm32f1xx_it.h`
  - `UART/Core/Src/stm32f1xx_it.c`
  - `UART/tests/__init__.py`
  - `UART/tests/test_can_source_contracts.py`

## 测试结果
| 测试 | 输入 | 预期结果 | 实际结果 | 状态 |
|------|------|---------|---------|------|
| 文档规划阶段 | 检查 F103/F407 代码基线 | 形成可执行的设计与实施计划 | 已完成设计与实现计划文档 | 通过 |
| F407 源代码约束测试 | `python -m unittest tests.test_can_multinode_source_contracts -v` | 4 个约束测试全部通过 | 4/4 通过 | 通过 |
| UART 源代码约束测试 | `python -m unittest tests.test_can_source_contracts -v` | 7 个约束测试全部通过 | 7/7 通过 | 通过 |
| F407 Keil 构建 | `UV4.exe -b ...F407.uvprojx -t F407` | 生成新的 `F407.axf` / `F407.htm` | 文件时间更新到 `2026-05-16 21:26:47` | 通过 |
| UART Keil 构建 | `UV4.exe -b ...UART.uvprojx` | 生成新的 `UART.axf` / `UART.htm` | 文件时间更新到 `2026-05-16 21:30:06` | 通过 |

## 错误日志
| 时间戳 | 错误 | 尝试次数 | 解决方案 |
|--------|------|---------|---------|
| 2026-05-16 | F103 早期 CAN 无应答 | 1 | 已在文档中记录：`TJA1050` 必须接 `5V` |

## 五问重启检查
| 问题 | 答案 |
|------|------|
| 我在哪里？ | 仓库整合完成，已推送到 GitHub，等待硬件联调 |
| 我要去哪里？ | i.MX6ULL CAN 多节点联调（阶段 9），然后稳定性验证（阶段 10） |
| 目标是什么？ | 让 i.MX6ULL 能同时挂载并区分 F103/F407 两个 CAN 节点 |
| 我学到了什么？ | 见 `findings.md`；本次新增：git 代理配置、编译产物清理、superpowers-zh 安装 |
| 我做了什么？ | 仓库整合、清理编译产物、补齐源码、编写 README、推送 GitHub、安装 superpowers |
