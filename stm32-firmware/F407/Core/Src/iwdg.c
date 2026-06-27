/**
  ******************************************************************************
  * File Name          : iwdg.c
  * Description        : Independent Watchdog (IWDG) configuration
  *                      LSI=32kHz, Prescaler=/128, Reload=1250 → 5.0s timeout
  ******************************************************************************
  */

#include "iwdg.h"

IWDG_HandleTypeDef hiwdg;

HAL_StatusTypeDef MX_IWDG_Init(void)
{
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_128;   /* 32kHz / 128 = 250Hz */
    hiwdg.Init.Reload = 1250U;                     /* 1250 / 250Hz = 5.0s */
    return HAL_IWDG_Init(&hiwdg);
}
