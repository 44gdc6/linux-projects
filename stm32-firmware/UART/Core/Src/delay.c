/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : delay.c
  * @brief          : Delay functions implementation using DWT cycle counter
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "delay.h"

/* Private variables ---------------------------------------------------------*/
static uint32_t g_freq_us = 0; /* CPU frequency in MHz (e.g. 8 for 8 MHz) */

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initialize DWT (Data Watchpoint and Trace) cycle counter for
  *         microsecond delays.
  * @note   The DWT cycle counter is a free-running 32-bit counter in the
  *         Cortex-M3 debug unit that counts at the core clock rate.
  *         Must be called once before using delay_us().
  * @retval None
  */
void delay_init(void)
{
    /* Enable DWT (debug unit) */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /* Reset cycle counter */
    DWT->CYCCNT = 0;

    /* Enable cycle counter */
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Get the CPU frequency in MHz from HAL */
    g_freq_us = HAL_RCC_GetHCLKFreq() / 1000000;

    if (g_freq_us == 0)
    {
        g_freq_us = 8; /* Default to 8 MHz if detection fails */
    }
}

/**
  * @brief  Delay for the specified number of microseconds using DWT.
  * @param  us: number of microseconds to delay (max ~536 seconds at 8 MHz)
  * @retval None
  */
void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * g_freq_us; /* Number of ticks for the delay */

    /* Wait until the counter has elapsed the required ticks */
    while ((DWT->CYCCNT - start) < ticks);
}

/**
  * @brief  Delay for the specified number of milliseconds.
  * @param  ms: number of milliseconds to delay
  * @retval None
  */
void delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}
