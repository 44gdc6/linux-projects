# UART Self-Test Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Temporarily switch the STM32 application to a UART-only self-test build that proves `USART1` TX and RX work on `PA9/PA10`.

**Architecture:** Keep the existing HAL startup path and `USART1` configuration, but remove CAN activity from `main()`. Replace the runtime behavior with a UART-ready banner, a once-per-second heartbeat, immediate echo of received bytes, and a visible UART error loop.

**Tech Stack:** STM32Cube HAL for STM32F1, Keil MDK-ARM project, Python `unittest` source-contract tests.

---

### Task 1: Retarget the source-contract tests to UART self-test behavior

**Files:**
- Modify: `tests/test_can_source_contracts.py`
- Test: `tests/test_can_source_contracts.py`

- [ ] **Step 1: Write the failing test**

```python
def test_main_runs_uart_self_test_only(self) -> None:
    source = read_text("Core/Src/main.c")
    self.assertIn("UART SELF TEST READY", source)
    self.assertIn("UART HEARTBEAT", source)
    self.assertIn("HAL_UART_Receive", source)
    self.assertNotIn("CAN_Link_Init()", source)
    self.assertNotIn("CAN_Link_Process()", source)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.test_can_source_contracts`
Expected: FAIL because `main.c` still contains CAN runtime calls and does not yet contain the UART self-test strings.

- [ ] **Step 3: Keep only the tests that still matter for this temporary mode**

```python
def test_main_has_uart_ready_banner(self) -> None:
    source = read_text("Core/Src/main.c")
    self.assertIn("UART SELF TEST READY", source)
    self.assertIn("BOOT: USART1 ready", source)

def test_error_handler_reports_over_uart(self) -> None:
    source = read_text("Core/Src/main.c")
    self.assertIn("ERROR: handler entered", source)
```

- [ ] **Step 4: Run test to keep it red for the right reason**

Run: `python -m unittest tests.test_can_source_contracts`
Expected: FAIL only on the new UART self-test assertions.

### Task 2: Convert `main.c` into UART-only self-test mode

**Files:**
- Modify: `Core/Src/main.c`
- Test: `tests/test_can_source_contracts.py`

- [ ] **Step 1: Add the minimal UART self-test helpers**

```c
static void Uart_SelfTest_Process(void);
```

and inside the helper:

```c
static void Uart_SelfTest_Process(void)
{
  static uint32_t last_heartbeat_tick = 0U;
  static uint32_t heartbeat_counter = 0U;
  uint8_t rx_byte;
  char message[48];

  if ((HAL_GetTick() - last_heartbeat_tick) >= 1000U)
  {
    last_heartbeat_tick = HAL_GetTick();
    snprintf(message, sizeof(message), "UART HEARTBEAT %lu\r\n", (unsigned long)heartbeat_counter++);
    Boot_Log(message);
  }

  if (HAL_UART_Receive(&huart1, &rx_byte, 1, 10) == HAL_OK)
  {
    HAL_UART_Transmit(&huart1, &rx_byte, 1, 100);
  }
}
```

- [ ] **Step 2: Remove CAN startup from `main()` and add the UART banner**

```c
MX_GPIO_Init();
MX_USART1_UART_Init();
g_uart1_ready = 1U;
Boot_Log("BOOT: HAL init done\r\n");
Boot_Log("BOOT: system clock configured\r\n");
Boot_Log("BOOT: GPIO ready\r\n");
Boot_Log("BOOT: USART1 ready\r\n");
Boot_Log("UART SELF TEST READY\r\n");
```

- [ ] **Step 3: Replace the infinite loop body**

```c
while (1)
{
  Uart_SelfTest_Process();
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python -m unittest tests.test_can_source_contracts`
Expected: PASS

### Task 3: Final verification and operator guidance

**Files:**
- Review: `Core/Src/main.c`
- Review: `tests/test_can_source_contracts.py`

- [ ] **Step 1: Inspect the final source for the expected UART markers**

Run: `rg -n "UART SELF TEST READY|UART HEARTBEAT|HAL_UART_Receive|ERROR: handler entered" Core/Src/main.c tests/test_can_source_contracts.py`
Expected: each marker appears in the expected file.

- [ ] **Step 2: Summarize the flash-time expectations**

Expected terminal behavior at `115200 8N1`:

```text
BOOT: HAL init done
BOOT: system clock configured
BOOT: GPIO ready
BOOT: USART1 ready
UART SELF TEST READY
UART HEARTBEAT 0
UART HEARTBEAT 1
...
```

Typed characters in the terminal should echo back immediately.
