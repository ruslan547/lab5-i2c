/**
  ******************************************************************************
  * @file    delay.c
  * @brief   Программные задержки на системном таймере SysTick.
  *
  * SysTick настраивается как свободно идущий 24-разрядный вычитающий счётчик,
  * тактируемый от частоты ядра (84 МГц). Прерывание не используется, поэтому
  * обработчик SysTick_Handler() не влияет на работу задержек.
  ******************************************************************************
  */

#include "delay.h"
#include "clock.h"

#define SYSTICK_MAX     0x00FFFFFFUL          /* разрядность счётчика   */
#define TICKS_PER_US    (SYSCLK_HZ/1000000UL) /* 84 такта на 1 мкс      */

void delay_init(void)
{
    SysTick->LOAD = SYSTICK_MAX;
    SysTick->VAL  = 0;
    /* источник тактирования – частота ядра, прерывание выключено */
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
}

void delay_us(uint32_t us)
{
    /* автоматический запуск таймера при первом вызове */
    if (!(SysTick->CTRL & SysTick_CTRL_ENABLE_Msk))
        delay_init();

    uint32_t ticks   = us * TICKS_PER_US;
    uint32_t last    = SysTick->VAL;
    uint32_t elapsed = 0;

    while (elapsed < ticks) {
        uint32_t now = SysTick->VAL;
        /* счётчик вычитающий, переполнение учитывается маской */
        elapsed += (last - now) & SYSTICK_MAX;
        last = now;
    }
}

void delay_ms(uint32_t ms)
{
    while (ms--)
        delay_us(1000UL);
}
