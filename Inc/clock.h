/**
  ******************************************************************************
  * @file    clock.h
  * @brief   Настройка системы тактирования МК STM32F401RE (CMSIS).
  ******************************************************************************
  */

#ifndef __CLOCK_H
#define __CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx.h"

/* Результирующие частоты после вызова RCC_HSI_init() */
#define SYSCLK_HZ   84000000UL   /* SYSCLK и AHB            */
#define PCLK1_HZ    42000000UL   /* APB1 (модули I2C1–I2C3) */
#define PCLK2_HZ    84000000UL   /* APB2                    */

/**
  * @brief Тактирование от HSI 16 МГц через PLL: SYSCLK = 84 МГц,
  *        AHB = 84 МГц, APB1 = 42 МГц, APB2 = 84 МГц.
  */
void RCC_HSI_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __CLOCK_H */
