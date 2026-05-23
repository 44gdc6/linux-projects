/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : delay.h
  * @brief          : Delay functions using DWT cycle counter
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __DELAY_H
#define __DELAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Exported functions prototypes ---------------------------------------------*/
void delay_init(void);      /* Initialize DWT cycle counter */
void delay_us(uint32_t us); /* Microsecond delay */
void delay_ms(uint32_t ms); /* Millisecond delay (wrapper for HAL_Delay) */

#ifdef __cplusplus
}
#endif

#endif /* __DELAY_H */
