// Лабораторная работа №5. Работа с датчиком давления
// BMP180 по интерфейсу I2C.
// МК STM32F401RE, модуль I2C3: PA8 – SCL, PC9 – SDA,
// частота шины 200 кГц (Fast mode).

// Библиотеки
#include "stm32f4xx.h"
#include "clock.h"
#include "delay.h"
#include "lcd1602.h"
#include <stdint.h>

//=============================================================
// МАКРООПРЕДЕЛЕНИЯ CMSIS
//=============================================================
/* Стандартные макросы CMSIS (приведены в stm32f4xx.h):
 * #define SET_BIT(REG, BIT)   ((REG) |= (BIT))
 * #define CLEAR_BIT(REG, BIT) ((REG) &= ~(BIT))
 * #define READ_BIT(REG, BIT)  ((REG) & (BIT))
 * #define WRITE_REG(REG, VAL) ((REG) = (VAL))
 * #define MODIFY_REG(REG, CLEARMASK, SETMASK) WRITE_REG((REG),
 *   (((READ_REG(REG)) & (~(CLEARMASK))) | (SETMASK)))
 */

//=============================================================
// НАСТРОЙКИ
//=============================================================
// 7-битный адрес BMP180 (0xEE/0xEF на шине)
#define BMP180_ADDR       0x77
// регистр идентификатора, всегда содержит 0x55
#define BMP180_CHIP_ID    0xD0
// регистр управления измерением
#define BMP180_MEAS_CTRL  0xF4
// таймаут ожидания флагов, циклов
#define I2C_TIMEOUT       10000UL
// тактовая частота шины APB1, Гц – определяется в clock.c
// (42 МГц при работе от PLL, 16 МГц при работе от HSI)
#define F_PCLK1           PCLK1_Hz
// частота шины I2C, Гц – по варианту 200 кГц
#define F_SCL             200000UL
// скважность: 0 – 2:1; 1 – 16:9
#define DUTY              0

// Расчёт ccr и trise по фактической частоте шины APB1
#if F_SCL > 100000UL        // Fast mode: 100...400 кГц
    #if DUTY == 0
        // T_low/T_high = 2:1 => T_scl = 3*CCR*T_pclk1 =>
        // CCR = F_PCLK1/(3*F_SCL) = 70
        #define ccr   (uint16_t)(F_PCLK1/(3UL*F_SCL))
        // время нарастания в Fm – 300 нс =>
        // TRISE = F_PCLK1[МГц]*0,3 + 1 = 13
        #define trise (uint16_t)(F_PCLK1/1000000UL*3UL/10UL+1UL)
    #elif DUTY == 1
        // T_low/T_high = 16:9 => CCR = F_PCLK1/(25*F_SCL)
        #define ccr   (uint16_t)(F_PCLK1/(25UL*F_SCL))
        #define trise (uint16_t)(F_PCLK1/1000000UL*3UL/10UL+1UL)
    #endif
#else                       // Standard mode: F_SCL <= 100 кГц
    #define ccr   (uint16_t)(F_PCLK1/(2UL*F_SCL))
    #define trise (uint16_t)(F_PCLK1/1000000UL+1UL)
#endif

//=============================================================
// ПРОТОТИПЫ
//=============================================================
// Инициализация I2C3 (200 кГц, PA8=SCL, PC9=SDA)
void I2C3_Init(void);
// Чтение одного байта из регистра ведомого
uint8_t I2C_ReadByte(uint8_t dev_addr, uint8_t reg);
// Запись одного байта в регистр ведомого
uint8_t I2C_WriteByte(uint8_t dev_addr, uint8_t reg,
                      uint8_t value);
// Вывод диагностики: номер шага и принятый байт
void LCD_diag(uint8_t stage, uint8_t data);

// Номер выполняемого шага обмена. Старший бит
// устанавливается, если на этом шаге истекло время
// ожидания. Значение видно в окне переменных отладчика
volatile uint8_t i2c_stage = 0;
// Вариант завершения приёма: 0 – А, 1 – Б (см. ниже)
volatile uint8_t i2c_mode = 0;

//=============================================================
// ОСНОВНАЯ ПРОГРАММА
//=============================================================
int main(void)
{
    // инициализация тактирования (HSI, 84 МГц)
    RCC_HSI_init();

    LCD_port_init();              // инициализация порта ЖКИ
    LCD_init();                   // инициализация ЖКИ
    LCD_pos(1, 1);                // позиция курсора
    LCD_send_string("LCD OK! ");  // вывод строки

    I2C3_Init();                  // инициализация I2C3
    LCD_send_string("I2C3 OK!");  // вывод строки

    // чтение идентификатора барометра BMP180
    uint8_t id;
    LCD_pos(2, 1);
    // чтение байта из регистра BMP180_CHIP_ID устройства
    // с адресом BMP180_ADDR
    id = I2C_ReadByte(BMP180_ADDR, BMP180_CHIP_ID);
    if (id != 0x55) {         // повтор по варианту Б
        i2c_mode = 1;
        id = I2C_ReadByte(BMP180_ADDR, BMP180_CHIP_ID);
    }
    // отображение результата на дисплее
    if (id == 0x55)
        LCD_send_string(i2c_mode ? "BMP180 OK! (B)  "
                                 : "BMP180 OK! (A)  ");
    else
        LCD_diag(i2c_stage, id);  // шаг сбоя и принятый байт

    // запись байта 0xFF в регистр BMP180_MEAS_CTRL
    // устройства с адресом BMP180_ADDR
    I2C_WriteByte(BMP180_ADDR, BMP180_MEAS_CTRL, 0xFF);

    // периодическое повторение обмена: позволяет наблюдать
    // осциллограммы шины и работу анализатора протокола
    while (1) {
        delay_ms(100);
        I2C_ReadByte(BMP180_ADDR, BMP180_CHIP_ID);
    }
}

//=============================================================
// Вывод диагностики в формате "ST=xx D=xx": номер шага,
// на котором прервался обмен, и последний принятый байт
//=============================================================
void LCD_diag(uint8_t stage, uint8_t data)
{
    static const char h[] = "0123456789ABCDEF";
    char s[17] = "ST=..  D=..     ";

    s[3] = h[(stage >> 4) & 0x0F];
    s[4] = h[stage & 0x0F];
    s[9] = h[(data >> 4) & 0x0F];
    s[10] = h[data & 0x0F];
    LCD_send_string(s);
}

//=============================================================
// Инициализация I2C3 (200 кГц, PA8=SCL, PC9=SDA)
//=============================================================
void I2C3_Init(void)
{
    // 1. Тактирование портов GPIOA, GPIOC и модуля I2C3
    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIOAEN);
    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIOCEN);
    SET_BIT(RCC->APB1ENR, RCC_APB1ENR_I2C3EN);

    // 2. PA8 – I2C3_SCL (AF4, Open-Drain, Pull-up, High)
    MODIFY_REG(GPIOA->MODER,            // режим вывода
               GPIO_MODER_MODER8_Msk,
               (2UL << GPIO_MODER_MODER8_Pos));
    SET_BIT(GPIOA->OTYPER,              // открытый сток
            GPIO_OTYPER_OT8_Msk);
    MODIFY_REG(GPIOA->OSPEEDR,          // высокая скорость
               GPIO_OSPEEDR_OSPEED8_Msk,
               (3UL << GPIO_OSPEEDR_OSPEED8_Pos));
    MODIFY_REG(GPIOA->PUPDR,            // подтяжка к VDD
               GPIO_PUPDR_PUPD8_Msk,
               (1UL << GPIO_PUPDR_PUPD8_Pos));
    MODIFY_REG(GPIOA->AFR[1],           // AF4 = I2C3
               GPIO_AFRH_AFSEL8_Msk,
               (4UL << GPIO_AFRH_AFSEL8_Pos));

    // 3. PC9 – I2C3_SDA (AF4, Open-Drain, Pull-up, High)
    MODIFY_REG(GPIOC->MODER,            // режим вывода
               GPIO_MODER_MODER9_Msk,
               (2UL << GPIO_MODER_MODER9_Pos));
    SET_BIT(GPIOC->OTYPER,              // открытый сток
            GPIO_OTYPER_OT9_Msk);
    MODIFY_REG(GPIOC->OSPEEDR,          // высокая скорость
               GPIO_OSPEEDR_OSPEED9_Msk,
               (3UL << GPIO_OSPEEDR_OSPEED9_Pos));
    MODIFY_REG(GPIOC->PUPDR,            // подтяжка к VDD
               GPIO_PUPDR_PUPD9_Msk,
               (1UL << GPIO_PUPDR_PUPD9_Pos));
    MODIFY_REG(GPIOC->AFR[1],           // AF4 = I2C3
               GPIO_AFRH_AFSEL9_Msk,
               (4UL << GPIO_AFRH_AFSEL9_Pos));

    // 4. Программный сброс модуля перед настройкой
    I2C3->CR1 &= ~I2C_CR1_PE;
    I2C3->CR1 |=  I2C_CR1_SWRST;
    I2C3->CR1 &= ~I2C_CR1_SWRST;

    // 5. Запись частотных параметров
    // частота тактирования модуля в МГц
    I2C3->CR2 &= ~I2C_CR2_FREQ;                  // очистка
    I2C3->CR2 |=  (uint16_t)(F_PCLK1/1000000UL); // частота, МГц

    if (F_SCL > 100000UL) {              // Fast mode
        if (DUTY == 0)                   // 200 кГц; 2:1
            I2C3->CCR = I2C_CCR_FS | ccr;
        else if (DUTY == 1)              // 200 кГц; 16:9
            I2C3->CCR = I2C_CCR_FS | I2C_CCR_DUTY | ccr;
    }
    else I2C3->CCR = ccr;                // Standard mode

    I2C3->TRISE = trise;   // время нарастания фронта
    I2C3->CR1 |= I2C_CR1_PE;  // включение модуля
    return;
}

//=============================================================
// Чтение одного байта из регистра ведомого
// (режим Master Receiver, 1 байт).
// Возвращает: прочитанный байт либо 0xFF при ошибке
//=============================================================
uint8_t I2C_ReadByte(uint8_t dev_addr, uint8_t reg)
{
    uint8_t data = 0;      // прочитанный байт
    // максимальное время ожидания, циклов
    uint32_t timeout = I2C_TIMEOUT;
    i2c_stage = 0x01;
    // ждём, пока флаг BUSY не станет 0 (шина свободна)
    while ((I2C3->SR2 & I2C_SR2_BUSY) && --timeout) {};
    if (!timeout) goto error;   // шина зависла

    // 1. START + адрес устройства (Write)
    I2C3->CR1 |= I2C_CR1_START;
    timeout = I2C_TIMEOUT;
    i2c_stage = 0x02;
    // ждём флаг SB (Start Bit)
    while (!(I2C3->SR1 & I2C_SR1_SB) && --timeout) {};
    if (!timeout) goto error;

    // 7-битный адрес + бит 0 (Write);
    // 0xFE = 11111110 – обнуление бита R/W
    I2C3->DR = (dev_addr << 1) & 0xFE;
    timeout = I2C_TIMEOUT;
    i2c_stage = 0x03;
    // ждём подтверждения адреса (ACK от ведомого)
    while (!(I2C3->SR1 & I2C_SR1_ADDR) && --timeout) {};
    if (!timeout) goto error;
    // ОБЯЗАТЕЛЬНО: чтение SR1, а затем SR2 сбрасывает
    // флаг ADDR, иначе шина остаётся заблокированной
    (void)I2C3->SR1;
    (void)I2C3->SR2;

    // 2. Отправка номера регистра (sub-address)
    I2C3->DR = reg;
    timeout = I2C_TIMEOUT;
    i2c_stage = 0x04;
    // ждём BTF (Byte Transfer Finished): TXE и сдвиговый
    // регистр пуст – здесь безопасен Repeated Start
    while (!(I2C3->SR1 & I2C_SR1_BTF) && --timeout) {};
    if (!timeout) goto error;

    i2c_stage = 0x05;
    // 3. Repeated Start + адрес устройства (Read)
    I2C3->CR1 |= I2C_CR1_START;
    timeout = I2C_TIMEOUT;
    while (!(I2C3->SR1 & I2C_SR1_SB) && --timeout) {};
    if (!timeout) goto error;

    i2c_stage = 0x06;
    I2C3->DR = (dev_addr << 1) | 0x01;  // адрес + бит Read
    timeout = I2C_TIMEOUT;
    while (!(I2C3->SR1 & I2C_SR1_ADDR) && --timeout) {};
    if (!timeout) goto error;

    // 4. Приём одного байта. Применяется один из двух
    // допустимых порядков завершения обмена
    if (i2c_mode == 0) {
        // А: по документации – ACK снимается до снятия
        // ADDR, условие STOP выдаётся до чтения DR
        I2C3->CR1 &= ~I2C_CR1_ACK;
        (void)I2C3->SR1;         // сброс флага ADDR:
        (void)I2C3->SR2;         // чтение SR1, затем SR2
        I2C3->CR1 |= I2C_CR1_STOP;
    }
    else {
        // Б: приём при установленном бите ACK, снятие
        // ACK и STOP – после появления принятого байта
        I2C3->CR1 |= I2C_CR1_ACK;
        (void)I2C3->SR1;
        (void)I2C3->SR2;
    }

    timeout = I2C_TIMEOUT;
    i2c_stage = 0x07;
    // ждём, пока в DR появится принятый байт
    while (!(I2C3->SR1 & (I2C_SR1_RXNE | I2C_SR1_BTF))
           && --timeout) {};
    if (!timeout) goto error;
    if (i2c_mode != 0) {
        I2C3->CR1 &= ~I2C_CR1_ACK;
        I2C3->CR1 |= I2C_CR1_STOP;
    }
    data = I2C3->DR;        // чтение байта сбрасывает RXNE

    timeout = I2C_TIMEOUT;
    // ожидание сброса бита STOP не критично: шина уже
    // освобождена, поэтому истечение времени ожидания
    // здесь ошибкой не считается
    while ((I2C3->CR1 & I2C_CR1_STOP) && --timeout) {};
    return data;

error:
    I2C3->CR1 |= I2C_CR1_STOP;  // освобождение шины
    i2c_stage |= 0x80;          // признак сбоя на шаге
    I2C3_Init();   // сброс модуля: снимает признак BUSY
    return 0xFF;                // код ошибки
}

//=============================================================
// Запись одного байта в регистр ведомого
// (режим Master Transmitter).
// Возвращает: 0 при успехе, 0xFF при ошибке
//=============================================================
uint8_t I2C_WriteByte(uint8_t dev_addr, uint8_t reg,
                      uint8_t value)
{
    uint32_t timeout = I2C_TIMEOUT;
    i2c_stage = 0x11;
    // ждём освобождения шины
    while ((I2C3->SR2 & I2C_SR2_BUSY) && --timeout) {};
    if (!timeout) goto error;

    // 1. START + адрес устройства (Write)
    I2C3->CR1 |= I2C_CR1_START;
    timeout = I2C_TIMEOUT;
    i2c_stage = 0x12;
    while (!(I2C3->SR1 & I2C_SR1_SB) && --timeout) {};
    if (!timeout) goto error;

    I2C3->DR = (dev_addr << 1) & 0xFE;
    i2c_stage = 0x13;
    timeout = I2C_TIMEOUT;
    while (!(I2C3->SR1 & I2C_SR1_ADDR) && --timeout) {};
    if (!timeout) goto error;
    (void)I2C3->SR1;        // сброс флага ADDR:
    (void)I2C3->SR2;        // чтение SR1, затем SR2

    i2c_stage = 0x14;
    // 2. Отправка номера регистра
    I2C3->DR = reg;
    timeout = I2C_TIMEOUT;
    while (!(I2C3->SR1 & I2C_SR1_BTF) && --timeout) {};
    if (!timeout) goto error;

    i2c_stage = 0x15;
    // 3. Отправка записываемого значения
    I2C3->DR = value;
    timeout = I2C_TIMEOUT;
    // ждём, пока последний байт уйдёт по шине
    while (!(I2C3->SR1 & I2C_SR1_BTF) && --timeout) {};
    if (!timeout) goto error;

    // 4. Условие STOP и завершение транзакции
    I2C3->CR1 |= I2C_CR1_STOP;
    timeout = I2C_TIMEOUT;
    // см. примечание в I2C_ReadByte
    while ((I2C3->CR1 & I2C_CR1_STOP) && --timeout) {};
    return 0;               // успех

error:
    I2C3->CR1 |= I2C_CR1_STOP;  // освобождение шины
    i2c_stage |= 0x80;          // признак сбоя на шаге
    I2C3_Init();   // сброс модуля: снимает признак BUSY
    return 0xFF;                // код ошибки
}
