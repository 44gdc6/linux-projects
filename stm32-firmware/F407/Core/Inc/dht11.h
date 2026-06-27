#ifndef __DHT11_H__
#define __DHT11_H__

#include "stm32f4xx_hal.h"

/* DHT11 data pin: PB6 (user must configure in CubeMX or gpio.c) */
#define DHT11_DATA_PORT     GPIOB
#define DHT11_DATA_PIN      GPIO_PIN_6

HAL_StatusTypeDef DHT11_Init(void);
HAL_StatusTypeDef DHT11_Read(uint8_t *humidity, uint8_t *temperature);

#endif
