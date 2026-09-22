#ifndef CONTROLLER_MAIN_H
#define CONTROLLER_MAIN_H

#include "stm32f1xx_hal.h"

/* Pin map taken from controller/13遥控原理图.pdf. */
#define KEY1_Pin GPIO_PIN_15
#define KEY1_GPIO_Port GPIOA
#define KEY2_Pin GPIO_PIN_11
#define KEY2_GPIO_Port GPIOB
#define KEY3_Pin GPIO_PIN_13
#define KEY3_GPIO_Port GPIOC
#define KEY4_Pin GPIO_PIN_14
#define KEY4_GPIO_Port GPIOC
#define KEY5_Pin GPIO_PIN_6
#define KEY5_GPIO_Port GPIOA
#define KEY6_Pin GPIO_PIN_7
#define KEY6_GPIO_Port GPIOA
#define KEY7_Pin GPIO_PIN_0
#define KEY7_GPIO_Port GPIOB
#define KEY8_Pin GPIO_PIN_1
#define KEY8_GPIO_Port GPIOB
#define CE_Pin GPIO_PIN_11
#define CE_GPIO_Port GPIOA
#define CSN_Pin GPIO_PIN_12
#define CSN_GPIO_Port GPIOB

void Error_Handler(void);

#endif
