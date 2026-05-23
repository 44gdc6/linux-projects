# UART Self-Test Design

## Goal

Temporarily switch the STM32 application into a UART-only self-test mode so we can verify whether `USART1` on `PA9/PA10` is physically working before returning to CAN bring-up.

## Scope

This change is intentionally narrow:

- Keep `HAL_Init()`, clock setup, GPIO init, and `USART1` init.
- Remove CAN initialization and CAN runtime processing from `main()`.
- Replace the current startup flow with a UART heartbeat and UART echo loop.
- Keep the existing source-contract tests, but retarget them to the UART self-test behavior.

This change does not attempt to validate CAN wiring, CAN bitrate, or Linux-side CAN setup.

## Recommended Approach

Use a pure `USART1` test mode with two behaviors:

1. On boot, print a short banner such as `UART SELF TEST READY`.
2. In the main loop:
   - transmit a heartbeat line once per second
   - poll for one received byte and echo it back immediately

This is the best fit because it proves both directions of the UART path:

- if heartbeat appears, `PA9 -> USB-TTL` is working
- if typed characters echo back, `PA10 <- USB-TTL` is also working

## Expected Observation

After flashing this test build and opening a serial terminal at `115200 8N1`, the user should see:

- an initial ready banner
- a repeating heartbeat every second
- typed characters echoed back

If there is still no UART output, the likely problem is outside the current CAN application logic, such as:

- wrong physical UART pins
- missing USB-TTL adapter
- incorrect terminal settings
- board not actually booting the flashed image

## Error Handling

If initialization fails after `USART1` is ready, `Error_Handler()` should continue printing a short repeating error line over UART so failure remains externally visible.

## Test Strategy

Keep the lightweight Python source-contract test approach and make it assert:

- UART self-test banner exists
- heartbeat string exists
- receive/echo path exists
- CAN runtime functions are no longer called from `main()`

## Implementation Notes

- Favor minimal edits in `Core/Src/main.c`.
- Leave `can.c` and related CAN support code in the tree for later reuse, but do not invoke it from the UART self-test build path.
- Keep the code easy to revert back to CAN bring-up once UART is proven healthy.
