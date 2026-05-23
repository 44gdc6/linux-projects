# i.MX6ULL CAN 多节点挂载实现计划

> **给执行代理的要求：** 实施本计划时，必须按任务逐项勾选执行；推荐使用 `superpowers:subagent-driven-development`，也可以用 `superpowers:executing-plans` 在当前会话内分批执行。

**目标：** 让 `i.MX6ULL` 在一条 `500 kbps` 的 CAN 总线上稳定区分并挂载 `F103` 与 `F407` 两个节点，实现心跳、定向命令和定向应答。

**架构：** 保持每个 STM32 工程现有的 CubeMX 框架不大改，分别在各自的 `can.c` / `main.c` 中补齐节点运行逻辑。协议层统一采用标准帧、固定 ID 分区和固定 8 字节载荷，Linux 侧先用 `can-utils` 完成联调。

**技术栈：** STM32Cube HAL（F1/F4）、Keil MDK-ARM、Python `unittest` 源代码约束测试、Linux `can-utils`。

---

## 文件结构与职责

- `d:\CubemxProject\F407\Core\Src\main.c`
  - F407 启动日志、CAN 初始化接入、主循环运行。
- `d:\CubemxProject\F407\Core\Inc\can.h`
  - F407 CAN 对外接口声明。
- `d:\CubemxProject\F407\Core\Src\can.c`
  - F407 节点协议逻辑、过滤器、收发、中断回调、错误日志。
- `d:\CubemxProject\F407\Core\Src\stm32f4xx_it.c`
  - F407 的 CAN 中断入口。
- `d:\CubemxProject\F407\tests\test_can_multinode_source_contracts.py`
  - F407 代码约束测试。
- `d:\CubemxProject\UART\Core\Src\main.c`
  - F103 主循环入口，继续调用 `CAN_Link_Init()` / `CAN_Link_Process()`。
- `d:\CubemxProject\UART\Core\Src\can.c`
  - F103 从单节点演示改成统一多节点节点协议。
- `d:\CubemxProject\UART\tests\test_can_source_contracts.py`
  - F103 代码约束测试更新。

---

### 任务 1：给 F407 建立源代码约束测试

**文件：**
- 创建：`d:\CubemxProject\F407\tests\test_can_multinode_source_contracts.py`
- 检查：`d:\CubemxProject\F407\Core\Src\main.c`
- 检查：`d:\CubemxProject\F407\Core\Src\can.c`
- 检查：`d:\CubemxProject\F407\Core\Src\stm32f4xx_it.c`

- [ ] **步骤 1：先写失败测试，锁定 F407 最小目标**

```python
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read_text(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8")


class TestF407CanMultinodeContracts(unittest.TestCase):
    def test_main_calls_can_link_runtime(self) -> None:
        source = read_text("Core/Src/main.c")
        self.assertIn("MX_CAN1_Init();", source)
        self.assertIn("MX_UART5_Init();", source)
        self.assertIn("CAN_Link_Init()", source)
        self.assertIn("CAN_Link_Process()", source)
        self.assertIn("BOOT: CAN link init ok", source)

    def test_can_source_declares_multinode_ids(self) -> None:
        source = read_text("Core/Src/can.c")
        self.assertIn("#define CAN_NODE_ID", source)
        self.assertIn("#define CAN_CMD_ID_BASE", source)
        self.assertIn("#define CAN_RESP_ID_BASE", source)
        self.assertIn("#define CAN_HEARTBEAT_ID_BASE", source)

    def test_can_source_enables_500k_runtime(self) -> None:
        source = read_text("Core/Src/can.c")
        self.assertIn("hcan1.Init.Prescaler = 2;", source)
        self.assertIn("hcan1.Init.TimeSeg1 = CAN_BS1_13TQ;", source)
        self.assertIn("hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;", source)
        self.assertIn("HAL_CAN_Start(&hcan1)", source)
        self.assertIn("HAL_CAN_ActivateNotification(&hcan1", source)

    def test_interrupt_file_wires_can_irqs(self) -> None:
        source = read_text("Core/Src/stm32f4xx_it.c")
        self.assertIn("void CAN1_RX0_IRQHandler(void)", source)
        self.assertIn("HAL_CAN_IRQHandler(&hcan1);", source)
```

- [ ] **步骤 2：运行测试，确认现在是红灯**

运行：

```powershell
cd d:\CubemxProject\F407
python -m unittest tests.test_can_multinode_source_contracts -v
```

预期：

```text
FAIL: main.c 还没有 CAN_Link_Init()/CAN_Link_Process()
FAIL: can.c 还没有多节点协议宏和运行期逻辑
FAIL: stm32f4xx_it.c 还没有 CAN 中断处理函数
```

- [ ] **步骤 3：若 `tests` 目录不存在，先补目录与 `__init__.py`**

```powershell
cd d:\CubemxProject\F407
mkdir tests
ni tests\__init__.py -ItemType File
```

- [ ] **步骤 4：再次运行测试，确保失败点只落在待实现逻辑上**

运行：

```powershell
cd d:\CubemxProject\F407
python -m unittest tests.test_can_multinode_source_contracts -v
```

预期：测试框架能启动，失败内容集中在协议逻辑缺失，而不是目录或导入错误。

---

### 任务 2：实现 F407 的统一节点协议

**文件：**
- 修改：`d:\CubemxProject\F407\Core\Inc\can.h`
- 修改：`d:\CubemxProject\F407\Core\Src\can.c`
- 修改：`d:\CubemxProject\F407\Core\Src\main.c`
- 修改：`d:\CubemxProject\F407\Core\Src\stm32f4xx_it.c`
- 测试：`d:\CubemxProject\F407\tests\test_can_multinode_source_contracts.py`

- [ ] **步骤 1：在 `can.h` 暴露运行期接口**

在 `/* USER CODE BEGIN Prototypes */` 中加入：

```c
HAL_StatusTypeDef CAN_Link_Init(void);
void CAN_Link_Process(void);
```

- [ ] **步骤 2：修正 F407 的 CAN 时序与基础配置**

将 `d:\CubemxProject\F407\Core\Src\can.c` 中的初始化改为：

```c
hcan1.Instance = CAN1;
hcan1.Init.Prescaler = 2;
hcan1.Init.Mode = CAN_MODE_NORMAL;
hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
hcan1.Init.TimeSeg1 = CAN_BS1_13TQ;
hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
hcan1.Init.TimeTriggeredMode = DISABLE;
hcan1.Init.AutoBusOff = DISABLE;
hcan1.Init.AutoWakeUp = DISABLE;
hcan1.Init.AutoRetransmission = ENABLE;
hcan1.Init.ReceiveFifoLocked = DISABLE;
hcan1.Init.TransmitFifoPriority = DISABLE;
```

并在 `HAL_CAN_MspInit()` 的 `USER CODE BEGIN CAN1_MspInit 1` 中加入：

```c
HAL_NVIC_SetPriority(CAN1_RX0_IRQn, 1, 0);
HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
HAL_NVIC_SetPriority(CAN1_SCE_IRQn, 1, 1);
HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);
```

- [ ] **步骤 3：在 `can.c` 增加节点协议宏、状态结构和日志函数**

把下面这段放到 `/* USER CODE BEGIN 0 */`：

```c
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define CAN_NODE_ID             0x02U
#define CAN_CMD_ID_BASE         0x300U
#define CAN_RESP_ID_BASE        0x200U
#define CAN_HEARTBEAT_ID_BASE   0x100U
#define CAN_FRAME_DLC           8U
#define CAN_HEARTBEAT_MS        1000U

typedef struct
{
  uint8_t last_cmd;
  uint8_t last_status;
  uint8_t last_seq;
  uint16_t heartbeat_counter;
  uint16_t rx_ok_counter;
  uint16_t tx_ok_counter;
  uint32_t last_heartbeat_tick;
} CAN_LinkState_t;

static CAN_LinkState_t g_can_link_state;

static void CAN_Link_Log(const char *message)
{
  if (message == NULL)
  {
    return;
  }

  HAL_UART_Transmit(&huart5, (uint8_t *)message, (uint16_t)strlen(message), 100);
}
```

- [ ] **步骤 4：补齐 `CAN_Link_Init()`、心跳发送、命令应答和回调**

在 `d:\CubemxProject\F407\Core\Src\can.c` 的 `/* USER CODE BEGIN 1 */` 中加入：

```c
static HAL_StatusTypeDef CAN_Link_SendHeartbeat(void);
static HAL_StatusTypeDef CAN_Link_SendResponse(uint8_t cmd, uint8_t seq, uint8_t status, uint8_t data0, uint8_t data1);
static void CAN_Link_HandleCommand(const CAN_RxHeaderTypeDef *rx_header, const uint8_t *rx_data);

HAL_StatusTypeDef CAN_Link_Init(void)
{
  CAN_FilterTypeDef filter = {0};

  memset(&g_can_link_state, 0, sizeof(g_can_link_state));
  g_can_link_state.last_heartbeat_tick = HAL_GetTick();

  filter.FilterBank = 0;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterIdHigh = 0x0000;
  filter.FilterIdLow = 0x0000;
  filter.FilterMaskIdHigh = 0x0000;
  filter.FilterMaskIdLow = 0x0000;
  filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  filter.FilterActivation = ENABLE;

  if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK)
  {
    CAN_Link_Log("CAN filter configuration failed\r\n");
    return HAL_ERROR;
  }

  if (HAL_CAN_Start(&hcan1) != HAL_OK)
  {
    CAN_Link_Log("CAN start failed\r\n");
    return HAL_ERROR;
  }

  if (HAL_CAN_ActivateNotification(&hcan1,
                                   CAN_IT_RX_FIFO0_MSG_PENDING |
                                   CAN_IT_ERROR_WARNING |
                                   CAN_IT_ERROR_PASSIVE |
                                   CAN_IT_BUSOFF |
                                   CAN_IT_LAST_ERROR_CODE |
                                   CAN_IT_ERROR) != HAL_OK)
  {
    CAN_Link_Log("CAN notification enable failed\r\n");
    return HAL_ERROR;
  }

  CAN_Link_Log("CAN link ready: 500kbps, node=0x02\r\n");
  return HAL_OK;
}

void CAN_Link_Process(void)
{
  if ((HAL_GetTick() - g_can_link_state.last_heartbeat_tick) >= CAN_HEARTBEAT_MS)
  {
    g_can_link_state.last_heartbeat_tick = HAL_GetTick();
    (void)CAN_Link_SendHeartbeat();
  }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  CAN_RxHeaderTypeDef rx_header;
  uint8_t rx_data[8];

  if (hcan->Instance != CAN1)
  {
    return;
  }

  if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK)
  {
    CAN_Link_Log("CAN RX read failed\r\n");
    return;
  }

  CAN_Link_HandleCommand(&rx_header, rx_data);
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
  char message[96];

  if (hcan->Instance != CAN1)
  {
    return;
  }

  snprintf(message,
           sizeof(message),
           "CAN error err=0x%08lX esr=0x%08lX\r\n",
           (unsigned long)HAL_CAN_GetError(hcan),
           (unsigned long)hcan->Instance->ESR);
  CAN_Link_Log(message);
}
```

`CAN_Link_HandleCommand()` 的核心判断保持如下：

```c
if (rx_header->IDE != CAN_ID_STD)
{
  return;
}

if (rx_header->StdId != (CAN_CMD_ID_BASE + CAN_NODE_ID))
{
  return;
}
```

`cmd` 处理先只做三条：

```c
switch (rx_data[0])
{
  case 0x01U:
    status = 0x00U;
    data0 = 0xA5U;
    data1 = CAN_NODE_ID;
    break;
  case 0x02U:
    status = g_can_link_state.last_status;
    data0 = (uint8_t)(g_can_link_state.heartbeat_counter & 0xFFU);
    data1 = (uint8_t)(g_can_link_state.rx_ok_counter & 0xFFU);
    break;
  case 0x03U:
    status = 0x00U;
    data0 = rx_data[2];
    data1 = rx_data[3];
    break;
  default:
    status = 0xE0U;
    break;
}
```

- [ ] **步骤 5：在 `main.c` 接入启动日志和主循环**

参考 `UART` 工程，在 `d:\CubemxProject\F407\Core\Src\main.c` 中加入：

```c
#include <string.h>

static uint8_t g_uart5_ready = 0U;

static void Boot_Log(const char *message)
{
  if (g_uart5_ready == 0U || message == NULL)
  {
    return;
  }

  HAL_UART_Transmit(&huart5, (uint8_t *)message, (uint16_t)strlen(message), 100);
}
```

初始化顺序改成：

```c
MX_GPIO_Init();
MX_CAN1_Init();
MX_UART5_Init();
g_uart5_ready = 1U;
Boot_Log("BOOT: HAL init done\r\n");
Boot_Log("BOOT: system clock configured\r\n");
Boot_Log("BOOT: GPIO ready\r\n");
Boot_Log("BOOT: UART5 ready\r\n");

if (CAN_Link_Init() != HAL_OK)
{
  Boot_Log("BOOT: CAN link init failed\r\n");
  Error_Handler();
}

Boot_Log("BOOT: CAN link init ok\r\n");
```

主循环改成：

```c
while (1)
{
  CAN_Link_Process();
  HAL_Delay(10);
}
```

- [ ] **步骤 6：补齐 `stm32f4xx_it.c` 的 CAN 中断入口**

在文件顶部外部变量区域加入：

```c
extern CAN_HandleTypeDef hcan1;
```

在 `/* USER CODE BEGIN 1 */` 区域加入：

```c
void CAN1_RX0_IRQHandler(void)
{
  HAL_CAN_IRQHandler(&hcan1);
}

void CAN1_SCE_IRQHandler(void)
{
  HAL_CAN_IRQHandler(&hcan1);
}
```

- [ ] **步骤 7：运行 F407 测试并编译**

运行：

```powershell
cd d:\CubemxProject\F407
python -m unittest tests.test_can_multinode_source_contracts -v
& 'E:\keil_51_32\UV4\UV4.exe' -b d:\CubemxProject\F407\MDK-ARM\F407.uvprojx -t F407
```

预期：

```text
unittest 全部 PASS
Build target 'F407'
0 Error(s), 0 Warning(s)
```

---

### 任务 3：把 F103 从单节点演示改成统一多节点节点

**文件：**
- 修改：`d:\CubemxProject\UART\Core\Src\can.c`
- 修改：`d:\CubemxProject\UART\tests\test_can_source_contracts.py`
- 检查：`d:\CubemxProject\UART\Core\Src\main.c`

- [ ] **步骤 1：先把 F103 的测试从 `0x555` 演示切到节点协议**

把测试断言改成围绕以下标志：

```python
self.assertIn("#define CAN_NODE_ID             0x01U", source)
self.assertIn("#define CAN_CMD_ID_BASE", source)
self.assertIn("#define CAN_RESP_ID_BASE", source)
self.assertIn("#define CAN_HEARTBEAT_ID_BASE", source)
self.assertIn("CAN link ready: 500kbps, node=0x01", source)
self.assertNotIn("#define CAN_LINK_TX_STDID       0x555U", source)
```

- [ ] **步骤 2：运行测试，确认它先失败**

运行：

```powershell
cd d:\CubemxProject\UART
python -m unittest tests.test_can_source_contracts -v
```

预期：失败点集中在 `can.c` 还保留 `0x555` 演示逻辑。

- [ ] **步骤 3：把 F103 的 `can.c` 改成与 F407 同构的节点协议**

核心替换点：

```c
#define CAN_NODE_ID             0x01U
#define CAN_CMD_ID_BASE         0x300U
#define CAN_RESP_ID_BASE        0x200U
#define CAN_HEARTBEAT_ID_BASE   0x100U
#define CAN_FRAME_DLC           8U
#define CAN_HEARTBEAT_MS        1000U
```

并删除当前这类单节点演示逻辑：

```c
#define CAN_LINK_TX_STDID       0x555U
tx_header.StdId = CAN_LINK_TX_STDID;
tx_data[0] = (uint8_t)(0x11U + payload_offset);
```

替换成与 F407 同样的三段逻辑：

```c
1. 周期发心跳 `0x101`
2. 收到 `0x301` 后按命令字回 `0x201`
3. UART1 打印 RX / TX / ERROR
```

- [ ] **步骤 4：保持 `main.c` 主流程不大改，只校准启动提示**

确认 `d:\CubemxProject\UART\Core\Src\main.c` 仍保持：

```c
MX_GPIO_Init();
MX_USART1_UART_Init();
MX_CAN_Init();

if (CAN_Link_Init() != HAL_OK)
{
  Boot_Log("BOOT: CAN link init failed\r\n");
  Error_Handler();
}

while (1)
{
  CAN_Link_Process();
  HAL_Delay(10);
}
```

只把提示文案改成节点模式，例如：

```c
Boot_Log("BOOT: CAN node init ok\r\n");
```

- [ ] **步骤 5：运行 F103 测试并编译**

运行：

```powershell
cd d:\CubemxProject\UART
python -m unittest tests.test_can_source_contracts -v
& 'E:\keil_51_32\UV4\UV4.exe' -b d:\CubemxProject\UART\MDK-ARM\UART.uvprojx
```

预期：

```text
unittest 全部 PASS
Build target 'UART'
0 Error(s), 0 Warning(s)
```

---

### 任务 4：在 i.MX6ULL 上做多节点硬件联调

**文件：**
- 检查：`d:\CubemxProject\F407\findings.md`
- 回填：`d:\CubemxProject\F407\progress.md`

- [ ] **步骤 1：先做硬件前置检查**

执行前逐项确认：

```text
1. F103 的 TJA1050 接 5V
2. F407 的 TJA1050 接 5V
3. 三端公共地连通
4. CANH 对 CANH，CANL 对 CANL
5. 总线只有两端终端电阻
6. i.MX6ULL 的 can0 已启用
```

- [ ] **步骤 2：在 i.MX6ULL 上配置 can0**

运行：

```sh
ip link set can0 down
ip link set can0 type can bitrate 500000
ip link set can0 up
ip -details link show can0
```

预期：`bitrate 500000`，接口状态为 `UP`。

- [ ] **步骤 3：先单独验证 F103**

终端 1：

```sh
candump can0
```

终端 2：

```sh
cansend can0 301#0101000000000000
cansend can0 301#0201000000000000
```

预期：

```text
周期看到 101#...
发送 301 后，看到 201#...
F103 串口打印 RX/TX 日志
```

- [ ] **步骤 4：单独验证 F407**

终端 2：

```sh
cansend can0 302#0101000000000000
cansend can0 302#0201000000000000
```

预期：

```text
周期看到 102#...
发送 302 后，看到 202#...
F407 串口打印 RX/TX 日志
```

- [ ] **步骤 5：两节点同时挂总线**

运行：

```sh
cansend can0 301#0101000000000000
cansend can0 302#0102000000000000
```

预期：

```text
101 和 102 心跳同时存在
301 只触发 201
302 只触发 202
没有错误节点替对方应答
```

- [ ] **步骤 6：若再次出现 `No buffer space available`，按固定顺序排查**

排查顺序写死如下：

```text
1. 先看 TJA1050 是否真有 5V
2. 再看公共地
3. 再看终端电阻是否只在两端
4. 再看 CANH/CANL 是否接反
5. 最后才怀疑协议代码
```

---

### 任务 5：回归记录与收尾

**文件：**
- 修改：`d:\CubemxProject\F407\progress.md`
- 修改：`d:\CubemxProject\F407\findings.md`

- [ ] **步骤 1：把实测结果写回 `progress.md`**

至少补齐：

```markdown
| 测试 | 输入 | 预期结果 | 实际结果 | 状态 |
|------|------|---------|---------|------|
| F103 单节点联调 | 301#0101000000000000 | 收到 201 响应 | ... | ... |
| F407 单节点联调 | 302#0101000000000000 | 收到 202 响应 | ... | ... |
| 双节点并联 | 同时观察 101/102 心跳 | 双节点都在线 | ... | ... |
```

- [ ] **步骤 2：把最终硬件结论写回 `findings.md`**

至少保留：

```markdown
- TJA1050 必须 5V 供电
- i.MX6ULL 侧 CAN 口自带终端电阻
- 双节点挂载时只保留总线两端终端
- 心跳 ID 与响应 ID 的规划已固定
```

- [ ] **步骤 3：准备下一轮功能扩展入口**

下一轮只从下面三项里选一项，不要混做：

```text
1. 给 F407 增加业务传感数据字段
2. 给 i.MX6ULL 写用户态主站程序
3. 把 F103/F407 公共协议字段抽成统一文档
```
