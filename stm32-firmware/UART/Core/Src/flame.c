/**
  ******************************************************************************
  * File Name          : flame.c
  * Description        : Flame sensor driver (digital input, active LOW)
  *                      Connected on PB5, active LOW when fire detected
  ******************************************************************************
  */

#include "flame.h"

void Flame_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin = FLAME_SENSOR_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(FLAME_SENSOR_PORT, &gpio);
}

/**
  * @brief  Read flame sensor status
  * @retval 0x01 = fire detected, 0x00 = safe
  */
uint8_t Flame_Read(void)
{
    if (HAL_GPIO_ReadPin(FLAME_SENSOR_PORT, FLAME_SENSOR_PIN) == GPIO_PIN_RESET) {
        return 0x01U;  /* Fire detected (active LOW) */
    }
    return 0x00U;  /* Safe */
}
