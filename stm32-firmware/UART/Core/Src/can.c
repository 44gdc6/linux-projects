/**
  ******************************************************************************
  * File Name          : CAN.c
  * Description        : This file provides code for the configuration
  *                      of the CAN instances.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "can.h"

/* USER CODE BEGIN 0 */
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define CAN_NODE_ID             0x01U
#define CAN_CMD_ID_BASE         0x300U
#define CAN_RESP_ID_BASE        0x200U
#define CAN_HEARTBEAT_ID_BASE   0x100U
#define CAN_FRAME_DLC           8U
#define CAN_HEARTBEAT_MS        1000U
#define CAN_TX_WAIT_MS          50U
#define CAN_STATUS_OK           0x00U
#define CAN_STATUS_BAD_CMD      0xE0U
#define CAN_STATUS_BAD_ARG      0xE1U

typedef struct
{
  CAN_RxHeaderTypeDef header;
  uint8_t data[CAN_FRAME_DLC];
  uint8_t pending;
} CAN_LinkRxFrame_t;

typedef struct
{
  uint8_t last_cmd;
  uint8_t last_status;
  uint8_t last_seq;
  uint16_t heartbeat_counter;
  uint16_t rx_ok_counter;
  uint16_t tx_ok_counter;
  uint32_t heartbeat_period_ms;
  uint32_t last_heartbeat_tick;
  uint32_t last_error_code;
  uint32_t last_error_status;
} CAN_LinkState_t;

static CAN_LinkRxFrame_t g_can_link_rx_frame;
static CAN_LinkState_t g_can_link_state;

static HAL_StatusTypeDef CAN_Link_SendFrame(uint32_t std_id, const uint8_t *data);
static HAL_StatusTypeDef CAN_Link_SendHeartbeat(void);
static HAL_StatusTypeDef CAN_Link_SendResponse(uint8_t cmd,
                                               uint8_t seq,
                                               uint8_t status,
                                               uint8_t data0,
                                               uint8_t data1,
                                               uint8_t data2,
                                               uint8_t data3);
static void CAN_Link_HandleCommand(const CAN_RxHeaderTypeDef *rx_header,
                                   const uint8_t *rx_data);

static void CAN_Link_Log(const char *message)
{
  if (message == NULL)
  {
    return;
  }

  HAL_UART_Transmit(&huart1, (uint8_t *)message, (uint16_t)strlen(message), 100);
}

static void CAN_Link_LogFrame(const char *prefix,
                              uint32_t std_id,
                              uint8_t dlc,
                              const uint8_t *data)
{
  char message[96];
  int offset;
  uint8_t index;

  if (data == NULL)
  {
    return;
  }

  offset = snprintf(message,
                    sizeof(message),
                    "%s ID=0x%03lX DLC=%u DATA=",
                    prefix,
                    (unsigned long)std_id,
                    dlc);
  if (offset < 0)
  {
    return;
  }

  for (index = 0U; index < dlc && index < CAN_FRAME_DLC; ++index)
  {
    offset += snprintf(&message[offset],
                       (size_t)(sizeof(message) - (size_t)offset),
                       "%02X%s",
                       data[index],
                       (index + 1U < dlc) ? " " : "");
    if ((size_t)offset >= sizeof(message))
    {
      return;
    }
  }

  (void)snprintf(&message[offset],
                 (size_t)(sizeof(message) - (size_t)offset),
                 "\r\n");
  CAN_Link_Log(message);
}

static void CAN_Link_LogStatus(const char *prefix)
{
  char message[160];
  uint32_t error_code;
  uint32_t esr_snapshot;
  uint32_t tsr_snapshot;
  uint32_t state;

  error_code = HAL_CAN_GetError(&hcan);
  esr_snapshot = hcan.Instance->ESR;
  tsr_snapshot = hcan.Instance->TSR;
  state = (uint32_t)HAL_CAN_GetState(&hcan);

  snprintf(message,
           sizeof(message),
           "%s err=0x%08lX esr=0x%08lX tsr=0x%08lX state=%lu\r\n",
           prefix,
           (unsigned long)error_code,
           (unsigned long)esr_snapshot,
           (unsigned long)tsr_snapshot,
           (unsigned long)state);
  CAN_Link_Log(message);
}
/* USER CODE END 0 */

CAN_HandleTypeDef hcan;

/* CAN init function */
void MX_CAN_Init(void)
{

  hcan.Instance = CAN1;
  hcan.Init.Prescaler = 1;
  hcan.Init.Mode = CAN_MODE_NORMAL;
  hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
  hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan.Init.TimeTriggeredMode = DISABLE;
  hcan.Init.AutoBusOff = DISABLE;
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = ENABLE;
  hcan.Init.ReceiveFifoLocked = DISABLE;
  hcan.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan) != HAL_OK)
  {
    Error_Handler();
  }

}

void HAL_CAN_MspInit(CAN_HandleTypeDef* canHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspInit 0 */

  /* USER CODE END CAN1_MspInit 0 */
    /* CAN1 clock enable */
    __HAL_RCC_CAN1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**CAN GPIO Configuration
    PA11     ------> CAN_RX
    PA12     ------> CAN_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_12;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN CAN1_MspInit 1 */
    HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
    HAL_NVIC_SetPriority(CAN1_SCE_IRQn, 1, 1);
    HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);

  /* USER CODE END CAN1_MspInit 1 */
  }
}

void HAL_CAN_MspDeInit(CAN_HandleTypeDef* canHandle)
{

  if(canHandle->Instance==CAN1)
  {
  /* USER CODE BEGIN CAN1_MspDeInit 0 */

  /* USER CODE END CAN1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_CAN1_CLK_DISABLE();

    /**CAN GPIO Configuration
    PA11     ------> CAN_RX
    PA12     ------> CAN_TX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11|GPIO_PIN_12);

  /* USER CODE BEGIN CAN1_MspDeInit 1 */
    HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    HAL_NVIC_DisableIRQ(CAN1_SCE_IRQn);

  /* USER CODE END CAN1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
static HAL_StatusTypeDef CAN_Link_SendFrame(uint32_t std_id, const uint8_t *data)
{
  CAN_TxHeaderTypeDef tx_header = {0};
  uint32_t mailbox;
  uint32_t tx_wait_start;

  if (data == NULL)
  {
    return HAL_ERROR;
  }

  tx_header.StdId = std_id;
  tx_header.ExtId = 0U;
  tx_header.IDE = CAN_ID_STD;
  tx_header.RTR = CAN_RTR_DATA;
  tx_header.DLC = CAN_FRAME_DLC;
  tx_header.TransmitGlobalTime = DISABLE;

  HAL_CAN_ResetError(&hcan);
  if (HAL_CAN_AddTxMessage(&hcan, &tx_header, (uint8_t *)data, &mailbox) != HAL_OK)
  {
    CAN_Link_LogStatus("CAN TX enqueue failed");
    return HAL_ERROR;
  }

  tx_wait_start = HAL_GetTick();
  while (HAL_CAN_IsTxMessagePending(&hcan, mailbox) != 0U)
  {
    if ((HAL_GetTick() - tx_wait_start) >= CAN_TX_WAIT_MS)
    {
      HAL_CAN_AbortTxRequest(&hcan, mailbox);
      CAN_Link_LogStatus("CAN TX pending timeout");
      return HAL_TIMEOUT;
    }
  }

  if (HAL_CAN_GetError(&hcan) != HAL_CAN_ERROR_NONE)
  {
    CAN_Link_LogStatus("CAN TX completed with error");
    return HAL_ERROR;
  }

  ++g_can_link_state.tx_ok_counter;
  CAN_Link_LogFrame("CAN TX", std_id, CAN_FRAME_DLC, data);
  return HAL_OK;
}

static HAL_StatusTypeDef CAN_Link_SendHeartbeat(void)
{
  uint8_t tx_data[CAN_FRAME_DLC] = {0};

  tx_data[0] = CAN_NODE_ID;
  tx_data[1] = (uint8_t)(g_can_link_state.heartbeat_counter & 0xFFU);
  tx_data[2] = (uint8_t)((g_can_link_state.heartbeat_counter >> 8) & 0xFFU);
  tx_data[3] = g_can_link_state.last_cmd;
  tx_data[4] = g_can_link_state.last_status;
  tx_data[5] = (uint8_t)(g_can_link_state.tx_ok_counter & 0xFFU);
  tx_data[6] = (uint8_t)(g_can_link_state.rx_ok_counter & 0xFFU);
  tx_data[7] = (uint8_t)(HAL_CAN_GetError(&hcan) & 0xFFU);

  ++g_can_link_state.heartbeat_counter;
  return CAN_Link_SendFrame(CAN_HEARTBEAT_ID_BASE + CAN_NODE_ID, tx_data);
}

static HAL_StatusTypeDef CAN_Link_SendResponse(uint8_t cmd,
                                               uint8_t seq,
                                               uint8_t status,
                                               uint8_t data0,
                                               uint8_t data1,
                                               uint8_t data2,
                                               uint8_t data3)
{
  uint8_t tx_data[CAN_FRAME_DLC] = {0};

  tx_data[0] = cmd;
  tx_data[1] = seq;
  tx_data[2] = status;
  tx_data[3] = CAN_NODE_ID;
  tx_data[4] = data0;
  tx_data[5] = data1;
  tx_data[6] = data2;
  tx_data[7] = data3;

  return CAN_Link_SendFrame(CAN_RESP_ID_BASE + CAN_NODE_ID, tx_data);
}

static void CAN_Link_HandleCommand(const CAN_RxHeaderTypeDef *rx_header,
                                   const uint8_t *rx_data)
{
  uint8_t cmd;
  uint8_t seq;
  uint8_t status = CAN_STATUS_OK;
  uint8_t data0 = 0U;
  uint8_t data1 = 0U;
  uint8_t data2 = 0U;
  uint8_t data3 = 0U;
  uint32_t period_ms;

  if (rx_header == NULL || rx_data == NULL)
  {
    return;
  }

  if (rx_header->IDE != CAN_ID_STD)
  {
    return;
  }

  if (rx_header->StdId != (CAN_CMD_ID_BASE + CAN_NODE_ID))
  {
    return;
  }

  g_can_link_state.last_cmd = rx_data[0];
  g_can_link_state.last_seq = rx_data[1];
  ++g_can_link_state.rx_ok_counter;
  CAN_Link_LogFrame("CAN RX", rx_header->StdId, rx_header->DLC, rx_data);

  cmd = rx_data[0];
  seq = rx_data[1];

  switch (cmd)
  {
    case 0x01U:
      data0 = 0xA5U;
      data1 = CAN_NODE_ID;
      break;

    case 0x02U:
      data0 = (uint8_t)(g_can_link_state.heartbeat_counter & 0xFFU);
      data1 = (uint8_t)((g_can_link_state.heartbeat_counter >> 8) & 0xFFU);
      data2 = (uint8_t)(g_can_link_state.tx_ok_counter & 0xFFU);
      data3 = (uint8_t)(g_can_link_state.rx_ok_counter & 0xFFU);
      break;

    case 0x03U:
      period_ms = ((uint32_t)rx_data[3] << 8) | (uint32_t)rx_data[2];
      if (period_ms < 100U)
      {
        status = CAN_STATUS_BAD_ARG;
      }
      else
      {
        g_can_link_state.heartbeat_period_ms = period_ms;
        data0 = rx_data[2];
        data1 = rx_data[3];
      }
      break;

    default:
      status = CAN_STATUS_BAD_CMD;
      break;
  }

  g_can_link_state.last_status = status;
  (void)CAN_Link_SendResponse(cmd, seq, status, data0, data1, data2, data3);
}

HAL_StatusTypeDef CAN_Link_Init(void)
{
  CAN_FilterTypeDef filter = {0};
  HAL_StatusTypeDef status;

  memset(&g_can_link_rx_frame, 0, sizeof(g_can_link_rx_frame));
  memset(&g_can_link_state, 0, sizeof(g_can_link_state));
  g_can_link_state.heartbeat_period_ms = CAN_HEARTBEAT_MS;
  g_can_link_state.last_heartbeat_tick = HAL_GetTick();
  g_can_link_state.last_error_code = HAL_CAN_ERROR_NONE;
  g_can_link_state.last_error_status = 0U;

  filter.FilterBank = 0;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterIdHigh = 0x0000;
  filter.FilterIdLow = 0x0000;
  filter.FilterMaskIdHigh = 0x0000;
  filter.FilterMaskIdLow = 0x0000;
  filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  filter.FilterActivation = ENABLE;

  status = HAL_CAN_ConfigFilter(&hcan, &filter);
  if (status != HAL_OK)
  {
    CAN_Link_Log("CAN filter configuration failed\r\n");
    return status;
  }

  status = HAL_CAN_Start(&hcan);
  if (status != HAL_OK)
  {
    CAN_Link_Log("CAN start failed\r\n");
    return status;
  }

  status = HAL_CAN_ActivateNotification(&hcan,
                                        CAN_IT_RX_FIFO0_MSG_PENDING |
                                        CAN_IT_ERROR_WARNING |
                                        CAN_IT_ERROR_PASSIVE |
                                        CAN_IT_BUSOFF |
                                        CAN_IT_LAST_ERROR_CODE |
                                        CAN_IT_ERROR);
  if (status != HAL_OK)
  {
    CAN_Link_Log("CAN RX notification enable failed\r\n");
    return status;
  }

  HAL_CAN_ResetError(&hcan);
  CAN_Link_Log("CAN link ready: 500kbps, node=0x01, RX FIFO0 interrupt enabled\r\n");
  return HAL_OK;
}

void CAN_Link_Process(void)
{
  if (g_can_link_rx_frame.pending != 0U)
  {
    CAN_Link_HandleCommand(&g_can_link_rx_frame.header, g_can_link_rx_frame.data);
    g_can_link_rx_frame.pending = 0U;
  }

  if ((HAL_GetTick() - g_can_link_state.last_heartbeat_tick) < g_can_link_state.heartbeat_period_ms)
  {
    return;
  }

  g_can_link_state.last_heartbeat_tick = HAL_GetTick();
  (void)CAN_Link_SendHeartbeat();
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *can_handle)
{
  if (can_handle == NULL || can_handle->Instance != CAN1)
  {
    return;
  }

  memset(g_can_link_rx_frame.data, 0, sizeof(g_can_link_rx_frame.data));
  if (HAL_CAN_GetRxMessage(can_handle,
                           CAN_RX_FIFO0,
                           &g_can_link_rx_frame.header,
                           g_can_link_rx_frame.data) == HAL_OK)
  {
    g_can_link_rx_frame.pending = 1U;
  }
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *can_handle)
{
  uint32_t error_code;
  uint32_t esr_snapshot;

  if (can_handle == NULL || can_handle->Instance != CAN1)
  {
    return;
  }

  error_code = HAL_CAN_GetError(can_handle);
  esr_snapshot = can_handle->Instance->ESR;
  if ((error_code != g_can_link_state.last_error_code) ||
      (esr_snapshot != g_can_link_state.last_error_status))
  {
    g_can_link_state.last_error_code = error_code;
    g_can_link_state.last_error_status = esr_snapshot;
    CAN_Link_LogStatus("CAN ERROR");
  }
}
/* USER CODE END 1 */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
