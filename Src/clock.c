/**
  ******************************************************************************
  * @file    clock.c
  * @brief   Настройка системы тактирования МК STM32F401RE (CMSIS).
  *
  * HSI = 16 МГц -> PLL (M = 16, N = 336, P = 4) -> SYSCLK = 84 МГц.
  * Делители шин: AHB = /1 (84 МГц), APB1 = /2 (42 МГц), APB2 = /1 (84 МГц).
  * Частота APB1 определяет поле CR2.FREQ модуля I2C3 (см. main.c).
  ******************************************************************************
  */

#include "clock.h"

void RCC_HSI_init(void)
{
    /* 1. Включение HSI и ожидание его готовности */
    SET_BIT(RCC->CR, RCC_CR_HSION);
    while (!READ_BIT(RCC->CR, RCC_CR_HSIRDY)) {};

    /* 2. Временно переключаем SYSCLK на HSI и выключаем PLL,
          чтобы изменить его настройки */
    MODIFY_REG(RCC->CFGR, RCC_CFGR_SW, RCC_CFGR_SW_HSI);
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {};
    CLEAR_BIT(RCC->CR, RCC_CR_PLLON);
    while (READ_BIT(RCC->CR, RCC_CR_PLLRDY)) {};

    /* 3. Питание: режим Scale 2 достаточен для 84 МГц */
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_PWREN);
    MODIFY_REG(PWR->CR, PWR_CR_VOS, PWR_CR_VOS_1);

    /* 4. Задержка доступа к Flash: 2 такта ожидания при 84 МГц и 3,3 В,
          плюс кэши команд и данных, предвыборка */
    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN |
                 FLASH_ACR_LATENCY_2WS;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_2WS) {};

    /* 5. Делители шин задаются до включения PLL */
    MODIFY_REG(RCC->CFGR, RCC_CFGR_HPRE,  RCC_CFGR_HPRE_DIV1);    /* AHB  = 84 МГц */
    MODIFY_REG(RCC->CFGR, RCC_CFGR_PPRE1, RCC_CFGR_PPRE1_DIV2);   /* APB1 = 42 МГц */
    MODIFY_REG(RCC->CFGR, RCC_CFGR_PPRE2, RCC_CFGR_PPRE2_DIV1);   /* APB2 = 84 МГц */

    /* 6. Настройка PLL: источник HSI, M = 16, N = 336, P = 4, Q = 7
          f_VCO_in  = 16 / 16  = 1 МГц
          f_VCO_out = 1 * 336  = 336 МГц
          f_PLL     = 336 / 4  = 84 МГц  */
    RCC->PLLCFGR = (16UL << RCC_PLLCFGR_PLLM_Pos) |
                   (336UL << RCC_PLLCFGR_PLLN_Pos) |
                   (1UL << RCC_PLLCFGR_PLLP_Pos) |   /* 01 = деление на 4 */
                   (7UL << RCC_PLLCFGR_PLLQ_Pos);
    CLEAR_BIT(RCC->PLLCFGR, RCC_PLLCFGR_PLLSRC);     /* источник PLL – HSI */

    /* 7. Запуск PLL и ожидание захвата частоты */
    SET_BIT(RCC->CR, RCC_CR_PLLON);
    while (!READ_BIT(RCC->CR, RCC_CR_PLLRDY)) {};

    /* 8. Переключение SYSCLK на выход PLL */
    MODIFY_REG(RCC->CFGR, RCC_CFGR_SW, RCC_CFGR_SW_PLL);
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {};

    /* 9. Обновление переменной CMSIS с текущей частотой ядра */
    SystemCoreClock = SYSCLK_HZ;
}
