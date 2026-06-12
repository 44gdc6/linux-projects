---
name: can-debug
description: CAN 总线协议调试助手。解析 candump/cansend 日志，验证帧 ID 分配，检查数据域内容，对比 STM32 发送逻辑。触发词：CAN调试、CAN分析、帧ID、candump、CAN协议
user-invocable: true
allowed-tools: "Read Write Edit Bash Glob Grep"
hooks:
  UserPromptSubmit:
    - hooks:
        - type: command
          command: "echo '[can-debug] 检测到 CAN 调试请求'"
metadata:
  version: "1.1.0"
---

# CAN 总线协议调试助手

分析本项目 CAN 总线通信，验证协议一致性。

## 使用方式

```
/can-debug log <文件>        # 解析 candump 日志文件
/can-debug dump <接口>       # 实时抓取 CAN 帧
/can-debug verify <节点>     # 验证 STM32 节点协议一致性
/can-debug test              # 运行 Python 契约测试
```

## 执行方式

### 方式 1: 使用脚本 (推荐)

**解析 candump 日志:**
```bash
python ${CLAUDE_PLUGIN_ROOT}/scripts/parse_candump.py <日志文件>
```

**验证节点协议一致性:**
```bash
python ${CLAUDE_PLUGIN_ROOT}/scripts/verify_can.py <节点ID> [日志文件]
```

**运行契约测试:**
```bash
# F407 节点
cd stm32-firmware/F407 && python -m unittest tests.test_can_multinode_source_contracts -v

# F103 节点
cd stm32-firmware/UART && python -m unittest tests.test_can_source_contracts -v
```

### 方式 2: 手动验证

按照下方检查项逐步验证。

## 项目 CAN 协议规范

### 物理层
- 协议: CAN 2.0A
- 波特率: 500 kbps
- 帧格式: 11-bit 标准帧

### 帧 ID 分配

| 帧类型 | ID 范围 | 方向 | 数据域 |
|--------|---------|------|--------|
| 心跳帧 | `0x100 + node_id` | 从→主 | [status, reserved, reserved, reserved, reserved, reserved, reserved, counter] |
| 命令帧 | `0x300 + node_id` | 主→从 | [cmd, param1, param2, reserved, reserved, reserved, reserved, reserved] |
| 应答帧 | `0x200 + node_id` | 从→主 | [cmd, status, data1, data2, data3, data4, reserved, reserved] |

### 节点 ID

| 节点 | ID | 平台 | 描述 |
|------|----|------|------|
| F103 最小系统 | `0x01` | STM32F103C8T6 | 传感器采集节点 |
| F407 扩展节点 | `0x02` | STM32F407ZGTx | 扩展传感器节点 |

### 命令定义

```c
// 命令类型
#define CMD_PING        0x01    // 心跳检测
#define CMD_READ_STATUS 0x02    // 读取状态
#define CMD_READ_DATA   0x03    // 读取传感器数据
#define CMD_SET_CONFIG  0x04    // 配置参数
#define CMD_RESET       0x05    // 复位节点

// 状态码
#define STATUS_OK       0x00    // 成功
#define STATUS_ERR      0x01    // 错误
#define STATUS_BUSY     0x02    // 忙
#define STATUS_TIMEOUT  0x03    // 超时
```

## 功能详解

### 1. 解析 candump 日志

```bash
python ${CLAUDE_PLUGIN_ROOT}/scripts/parse_candump.py can_log.txt
```

输出格式:
```
(1234.567) can0 1FF [8] 01 02 03 04 05 06 07 08
  -> 心跳: status=OK, counter=8
```

自动识别:
- 帧类型 (心跳/命令/应答)
- 节点 ID
- 数据域含义
- 异常帧 (ID 不符合规范)

### 2. 实时抓取

```bash
candump can0 | tee can_log.txt
```

然后解析:
```bash
python ${CLAUDE_PLUGIN_ROOT}/scripts/parse_candump.py can_log.txt
```

### 3. 验证 STM32 节点

```bash
python ${CLAUDE_PLUGIN_ROOT}/scripts/verify_can.py 0x01 can_log.txt    # 验证 F103 节点
python ${CLAUDE_PLUGIN_ROOT}/scripts/verify_can.py 0x02 can_log.txt    # 验证 F407 节点
```

验证项:
- [ ] 心跳帧 ID 是否正确 (0x100 + node_id)
- [ ] 心跳间隔是否稳定 (建议 1s)
- [ ] 应答帧是否对应命令
- [ ] 数据域长度是否为 8 字节
- [ ] 状态码是否符合定义

### 4. 运行契约测试

```bash
python ${CLAUDE_PLUGIN_ROOT}/scripts/verify_can.py 0x01    # F103 节点
python ${CLAUDE_PLUGIN_ROOT}/scripts/verify_can.py 0x02    # F407 节点
```

## 常见问题诊断

### 问题 1: 无数据

```
检查项:
1. CAN 接口是否启动: ip link set can0 up type can bitrate 500000
2. 终端电阻是否连接 (120Ω)
3. STM32 是否正常运行
```

### 问题 2: 数据错误

```
检查项:
1. 波特率是否匹配 (500kbps)
2. 数据长度是否为 8 字节 (不足需填充)
3. 字节序是否正确 (大端/小端)
```

### 问题 3: ID 不符合规范

```
检查项:
1. 心跳帧是否在 0x100-0x1FF 范围
2. 应答帧是否在 0x200-0x2FF 范围
3. 命令帧是否在 0x300-0x3FF 范围
4. node_id 是否为 0x01 或 0x02
```

## 参考文件

- CAN 协议定义: `stm32-firmware/F407/Core/Inc/can.h`
- 帧编解码: `project/app/src/comm/can_node.c`
- 契约测试: `stm32-firmware/*/tests/`
- 项目文档: `stm32-firmware/F407/docs/`

## 脚本说明

| 脚本 | 用途 |
|------|------|
| `scripts/parse_candump.py` | 解析 candump 日志，输出可读格式 |
| `scripts/verify_can.py` | 验证节点协议一致性，运行契约测试 |
