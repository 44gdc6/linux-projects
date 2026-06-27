/**
  ******************************************************************************
  * File Name          : iwdg.c
  * Description        : Independent Watchdog (IWDG) configuration
  *                      LSI=40kHz, Prescaler=/128, Reload=2500 → 8.0s timeout
  ******************************************************************************
  */

#include "iwdg.h"

IWDG_HandleTypeDef hiwdg;

HAL_StatusTypeDef MX_IWDG_Init(void)
{
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_128;   /* 40kHz / 128 = 312.5Hz */
    hiwdg.Init.Reload = 2500U;                     /* 2500 / 312.5Hz = 8.0s */
    return HAL_IWDG_Init(&hiwdg);
}
