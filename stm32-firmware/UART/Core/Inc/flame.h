#ifndef __FLAME_H__
#define __FLAME_H__

#include "stm32f1xx_hal.h"

/* Flame sensor data pin: PB5 (active LOW when fire detected) */
#define FLAME_SENSOR_PORT   GPIOB
#define FLAME_SENSOR_PIN    GPIO_PIN_5

void Flame_Init(void);
uint8_t Flame_Read(void);

#endif
