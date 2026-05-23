from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read_text(rel_path: str) -> str:
    return (ROOT / rel_path).read_text(encoding="utf-8")


class TestF407NodeProtocolContracts(unittest.TestCase):
    def test_main_brings_up_serial_before_can_runtime(self) -> None:
        source = read_text("Core/Src/main.c")
        self.assertIn("MX_USART1_UART_Init();", source)
        self.assertIn("MX_UART5_Init();", source)
        self.assertIn("MX_CAN1_Init();", source)
        self.assertIn("CAN_Link_Init()", source)
        self.assertIn("CAN_Link_Process()", source)
        self.assertLess(source.index("MX_USART1_UART_Init();"), source.index("MX_UART5_Init();"))
        self.assertLess(source.index("MX_UART5_Init();"), source.index("MX_CAN1_Init();"))

    def test_main_has_visible_boot_logs(self) -> None:
        source = read_text("Core/Src/main.c")
        self.assertIn("BOOT: USART1 ready", source)
        self.assertIn("BOOT: HAL init done", source)
        self.assertIn("BOOT: system clock configured", source)
        self.assertIn("BOOT: GPIO ready", source)
        self.assertIn("BOOT: UART5 ready", source)
        self.assertIn("BOOT: CAN link init ok", source)
        self.assertIn("ERROR: handler entered", source)

    def test_usart_source_broadcasts_logs_on_both_debug_ports(self) -> None:
        source = read_text("Core/Src/usart.c")
        self.assertIn("huart1.Instance = USART1;", source)
        self.assertIn("huart5.Instance = UART5;", source)
        self.assertIn("PA9     ------> USART1_TX", source)
        self.assertIn("PA10     ------> USART1_RX", source)
        self.assertIn("PC12     ------> UART5_TX", source)
        self.assertIn("PD2     ------> UART5_RX", source)
        self.assertIn("Debug_Log_TryWrite(&huart1, message);", source)
        self.assertIn("Debug_Log_TryWrite(&huart5, message);", source)

    def test_can_source_uses_500k_timing(self) -> None:
        source = read_text("Core/Src/can.c")
        self.assertIn("hcan1.Init.Prescaler = 2;", source)
        self.assertIn("hcan1.Init.TimeSeg1 = CAN_BS1_13TQ;", source)
        self.assertIn("hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;", source)
        self.assertIn("hcan1.Init.AutoRetransmission = ENABLE;", source)

    def test_can_source_uses_f407_node_protocol_ids(self) -> None:
        source = read_text("Core/Src/can.c")
        self.assertIn("#define CAN_NODE_ID             0x02U", source)
        self.assertIn("#define CAN_CMD_ID_BASE         0x300U", source)
        self.assertIn("#define CAN_RESP_ID_BASE        0x200U", source)
        self.assertIn("#define CAN_HEARTBEAT_ID_BASE   0x100U", source)
        self.assertIn("CAN link ready: 500kbps, node=0x02", source)
        self.assertIn("CAN_HEARTBEAT_ID_BASE + CAN_NODE_ID", source)
        self.assertIn("CAN_RESP_ID_BASE + CAN_NODE_ID", source)
        self.assertNotIn("#define CAN_AUTO_TX_STDID", source)

    def test_can_source_handles_ping_status_and_heartbeat_period_commands(self) -> None:
        source = read_text("Core/Src/can.c")
        self.assertIn("case 0x01U:", source)
        self.assertIn("case 0x02U:", source)
        self.assertIn("case 0x03U:", source)
        self.assertIn("g_can_link_state.heartbeat_period_ms = period_ms;", source)

    def test_can_source_keeps_uart_visible_error_diagnostics(self) -> None:
        source = read_text("Core/Src/can.c")
        self.assertIn("CAN TX enqueue failed", source)
        self.assertIn("CAN TX pending timeout", source)
        self.assertIn("CAN TX completed with error", source)
        self.assertIn("CAN ERROR", source)
        self.assertIn("hcan1.Instance->TSR", source)
        self.assertIn("HAL_CAN_RxFifo0MsgPendingCallback", source)

    def test_interrupt_file_still_wires_can_irq_handlers(self) -> None:
        source = read_text("Core/Src/stm32f4xx_it.c")
        self.assertIn('#include "can.h"', source)
        self.assertIn("void CAN1_RX0_IRQHandler(void)", source)
        self.assertIn("void CAN1_SCE_IRQHandler(void)", source)
        self.assertIn("HAL_CAN_IRQHandler(&hcan1);", source)


if __name__ == "__main__":
    unittest.main()
