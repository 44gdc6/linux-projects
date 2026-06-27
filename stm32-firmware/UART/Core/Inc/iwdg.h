#ifndef __IWDG_H__
#define __IWDG_H__

#include "stm32f1xx_hal.h"

extern IWDG_HandleTypeDef hiwdg;

HAL_StatusTypeDef MX_IWDG_Init(void);

#endif
