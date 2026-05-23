import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class CanLinkContractTests(unittest.TestCase):
    def test_can_init_uses_500kbps_timing(self) -> None:
        source = read_text("Core/Src/can.c")
        self.assertIn("hcan.Init.Prescaler = 1;", source)
        self.assertIn("hcan.Init.TimeSeg1 = CAN_BS1_13TQ;", source)
        self.assertIn("hcan.Init.TimeSeg2 = CAN_BS2_2TQ;", source)

    def test_main_reenables_can_runtime(self) -> None:
        source = read_text("Core/Src/main.c")
        self.assertIn('#include "can.h"', source)
        self.assertIn("MX_CAN_Init();", source)
        self.assertIn("CAN_Link_Init()", source)
        self.assertIn("CAN_Link_Process()", source)

    def test_can_link_uses_multinode_protocol_ids(self) -> None:
        source = read_text("Core/Src/can.c")
        self.assertIn("#define CAN_NODE_ID             0x01U", source)
        self.assertIn("#define CAN_CMD_ID_BASE         0x300U", source)
        self.assertIn("#define CAN_RESP_ID_BASE        0x200U", source)
        self.assertIn("#define CAN_HEARTBEAT_ID_BASE   0x100U", source)
        self.assertIn("CAN link ready: 500kbps, node=0x01", source)
        self.assertIn("CAN_HEARTBEAT_ID_BASE + CAN_NODE_ID", source)
        self.assertIn("CAN_RESP_ID_BASE + CAN_NODE_ID", source)
        self.assertNotIn("#define CAN_LINK_TX_STDID       0x555U", source)

    def test_can_link_has_uart_visible_tx_error_diagnostics(self) -> None:
        source = read_text("Core/Src/can.c")
        self.assertIn("CAN TX pending timeout", source)
        self.assertIn("HAL_CAN_GetError(&hcan)", source)
        self.assertIn("HAL_CAN_AbortTxRequest(&hcan, mailbox)", source)
        self.assertIn("HAL_CAN_ErrorCallback", source)

    def test_can_link_handles_ping_status_and_heartbeat_period_commands(self) -> None:
        source = read_text("Core/Src/can.c")
        self.assertIn("case 0x01U:", source)
        self.assertIn("case 0x02U:", source)
        self.assertIn("case 0x03U:", source)
        self.assertIn("g_can_link_state.heartbeat_period_ms = period_ms;", source)

    def test_main_has_uart_visible_can_startup_logs(self) -> None:
        source = read_text("Core/Src/main.c")
        self.assertIn("BOOT: USART1 ready", source)
        self.assertIn("BOOT: CAN link init ok", source)

    def test_error_handler_reports_over_uart(self) -> None:
        source = read_text("Core/Src/main.c")
        self.assertIn("ERROR: handler entered", source)


if __name__ == "__main__":
    unittest.main()
