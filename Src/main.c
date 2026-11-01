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
// Освобождение шины и сброс модуля после сбоя
static void I2C3_Recover(void);
// Вывод результата чтения идентификатора на ЖКИ
static void LCD_result(uint8_t id);

// Программная реализация шины на тех же выводах
void SW_I2C_Init(void);
uint8_t SW_ReadByte(uint8_t dev_addr, uint8_t reg);
uint8_t SW_WriteByte(uint8_t dev_addr, uint8_t reg,
                     uint8_t value);

// Номер шага обмена, на котором произошёл сбой.
// Аппаратный модуль (истёк таймаут ожидания флага):
// 1 – BUSY, 2 – SB, 3 – ADDR (W), 4 – BTF, 5 – SB (Sr),
// 6 – ADDR (R), 7 – RXNE; при записи – 11...15.
// Программная шина (ведомый ответил NACK):
// 21 – адрес (W), 22 – номер регистра, 23 – адрес (R);
// при записи – 31...33
volatile uint8_t i2c_step = 0;
// 1 – обмен выполняется программной реализацией шины
volatile uint8_t sw_used = 0;

//=============================================================
// ОСНОВНАЯ ПРОГРАММА
//=============================================================
int main(void)
{
    // инициализация тактирования (HSI -> PLL, 84 МГц)
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
    // Модель STM32F401 в Proteus не выводит сигналы
    // модуля I2C3 на выводы PA8/PC9 (флаг SB ставится,
    // а шина остаётся в покое). В этом случае обмен
    // продолжается программной реализацией шины
    if (id != 0x55) {
        SW_I2C_Init();
        sw_used = 1;
        id = SW_ReadByte(BMP180_ADDR, BMP180_CHIP_ID);
    }
    LCD_result(id);         // отображение результата

    // запись байта 0xFF в регистр BMP180_MEAS_CTRL
    // устройства с адресом BMP180_ADDR
    if (sw_used) SW_WriteByte(BMP180_ADDR, BMP180_MEAS_CTRL, 0xFF);
    else         I2C_WriteByte(BMP180_ADDR, BMP180_MEAS_CTRL, 0xFF);

    // периодическое повторение чтения: обмен попадает
    // в окно осциллографа и отладчика I2C в любой момент
    while (1) {
        delay_ms(100);
        if (sw_used) id = SW_ReadByte(BMP180_ADDR, BMP180_CHIP_ID);
        else         id = I2C_ReadByte(BMP180_ADDR, BMP180_CHIP_ID);
        LCD_pos(2, 1);
        LCD_result(id);
    }
}

//=============================================================
// Вывод "BMP180 OK!" либо "FAIL ST=n D=xx": шаг сбоя
// и принятый байт (в шестнадцатеричном виде)
//=============================================================
static void LCD_result(uint8_t id)
{
    static const char h[] = "0123456789ABCDEF";
    char s[17] = "FAIL ST=.. D=.. ";

    if (id == 0x55) {
        LCD_send_string(sw_used ? "BMP180 OK! (SW) "
                                : "BMP180 OK! (HW) ");
        return;
    }
    s[8]  = (char)('0' + i2c_step / 10);
    s[9]  = (char)('0' + i2c_step % 10);
    s[13] = h[id >> 4];
    s[14] = h[id & 0x0F];
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
}

//=============================================================
// Чтение одного байта из регистра ведомого:
// S – адрес+W – номер регистра – Sr – адрес+R – байт – NACK – P
// (режим Master Receiver, 1 байт, RM0368 п. 18.3.3).
// Возвращает: прочитанный байт либо 0xFF при ошибке
//=============================================================
uint8_t I2C_ReadByte(uint8_t dev_addr, uint8_t reg)
{
    uint8_t data;          // прочитанный байт
    // максимальное время ожидания, циклов
    uint32_t timeout = I2C_TIMEOUT;

    // ждём, пока флаг BUSY не станет 0 (шина свободна)
    i2c_step = 1;
    while ((I2C3->SR2 & I2C_SR2_BUSY) && --timeout) {};
    if (!timeout) goto error;   // шина зависла

    // 1. START + адрес устройства (Write)
    I2C3->CR1 |= I2C_CR1_START;
    timeout = I2C_TIMEOUT;
    // ждём флаг SB (Start Bit)
    i2c_step = 2;
    while (!(I2C3->SR1 & I2C_SR1_SB) && --timeout) {};
    if (!timeout) goto error;

    // 7-битный адрес + бит 0 (Write);
    // 0xFE = 11111110 – обнуление бита R/W
    I2C3->DR = (dev_addr << 1) & 0xFE;
    timeout = I2C_TIMEOUT;
    // ждём подтверждения адреса (ACK от ведомого)
    i2c_step = 3;
    while (!(I2C3->SR1 & I2C_SR1_ADDR) && --timeout) {};
    if (!timeout) goto error;
    // ОБЯЗАТЕЛЬНО: чтение SR1, а затем SR2 сбрасывает
    // флаг ADDR, иначе шина остаётся заблокированной
    (void)I2C3->SR1;
    (void)I2C3->SR2;

    // 2. Отправка номера регистра (sub-address)
    I2C3->DR = reg;
    timeout = I2C_TIMEOUT;
    // ждём BTF: байт передан, сдвиговый регистр пуст –
    // здесь безопасен повторный старт
    i2c_step = 4;
    while (!(I2C3->SR1 & I2C_SR1_BTF) && --timeout) {};
    if (!timeout) goto error;

    // 3. Repeated Start (без STOP) + адрес устройства (Read)
    I2C3->CR1 |= I2C_CR1_START;
    timeout = I2C_TIMEOUT;
    i2c_step = 5;
    while (!(I2C3->SR1 & I2C_SR1_SB) && --timeout) {};
    if (!timeout) goto error;

    I2C3->DR = (dev_addr << 1) | 0x01;  // адрес + бит Read
    timeout = I2C_TIMEOUT;
    i2c_step = 6;
    while (!(I2C3->SR1 & I2C_SR1_ADDR) && --timeout) {};
    if (!timeout) goto error;

    // 4. Приём одного байта: ACK снимается ДО сброса ADDR,
    // чтобы после байта ведущий выдал NACK, затем
    // сразу задаётся STOP
    I2C3->CR1 &= ~I2C_CR1_ACK;
    (void)I2C3->SR1;        // сброс флага ADDR
    (void)I2C3->SR2;
    I2C3->CR1 |= I2C_CR1_STOP;

    timeout = I2C_TIMEOUT;
    // ждём, пока в DR появится принятый байт
    i2c_step = 7;
    while (!(I2C3->SR1 & I2C_SR1_RXNE) && --timeout) {};
    if (!timeout) goto error;
    data = I2C3->DR;        // чтение байта сбрасывает RXNE

    // ждём завершения условия STOP на шине
    timeout = I2C_TIMEOUT;
    while ((I2C3->CR1 & I2C_CR1_STOP) && --timeout) {};
    I2C3->CR1 |= I2C_CR1_ACK;   // восстановление ACK
    i2c_step = 0;           // обмен завершён успешно
    return data;

error:
    I2C3_Recover();
    return 0xFF;            // код ошибки
}

//=============================================================
// Запись одного байта в регистр ведомого:
// S – адрес+W – номер регистра – значение – P
// (режим Master Transmitter).
// Возвращает: 0 при успехе, 0xFF при ошибке
//=============================================================
uint8_t I2C_WriteByte(uint8_t dev_addr, uint8_t reg,
                      uint8_t value)
{
    uint32_t timeout = I2C_TIMEOUT;
    // ждём освобождения шины
    i2c_step = 11;
    while ((I2C3->SR2 & I2C_SR2_BUSY) && --timeout) {};
    if (!timeout) goto error;

    // 1. START + адрес устройства (Write)
    I2C3->CR1 |= I2C_CR1_START;
    timeout = I2C_TIMEOUT;
    i2c_step = 12;
    while (!(I2C3->SR1 & I2C_SR1_SB) && --timeout) {};
    if (!timeout) goto error;

    I2C3->DR = (dev_addr << 1) & 0xFE;
    timeout = I2C_TIMEOUT;
    i2c_step = 13;
    while (!(I2C3->SR1 & I2C_SR1_ADDR) && --timeout) {};
    if (!timeout) goto error;
    (void)I2C3->SR1;        // сброс флага ADDR:
    (void)I2C3->SR2;        // чтение SR1, затем SR2

    // 2. Отправка номера регистра
    I2C3->DR = reg;
    timeout = I2C_TIMEOUT;
    i2c_step = 14;
    while (!(I2C3->SR1 & I2C_SR1_TXE) && --timeout) {};
    if (!timeout) goto error;

    // 3. Отправка записываемого значения
    I2C3->DR = value;
    timeout = I2C_TIMEOUT;
    // ждём, пока последний байт уйдёт по шине
    i2c_step = 15;
    while (!(I2C3->SR1 & I2C_SR1_BTF) && --timeout) {};
    if (!timeout) goto error;

    // 4. Условие STOP и завершение транзакции
    I2C3->CR1 |= I2C_CR1_STOP;
    timeout = I2C_TIMEOUT;
    while ((I2C3->CR1 & I2C_CR1_STOP) && --timeout) {};
    i2c_step = 0;
    return 0;               // успех

error:
    I2C3_Recover();
    return 0xFF;            // код ошибки
}

//=============================================================
// Освобождение шины после сбоя: условие STOP, затем
// повторная инициализация модуля (снимает признак BUSY)
//=============================================================
static void I2C3_Recover(void)
{
    uint32_t timeout = I2C_TIMEOUT;

    I2C3->CR1 |= I2C_CR1_STOP;
    // даём условию STOP выйти на шину до сброса модуля
    while ((I2C3->CR1 & I2C_CR1_STOP) && --timeout) {};
    I2C3_Init();
}

//=============================================================
// ПРОГРАММНАЯ РЕАЛИЗАЦИЯ ШИНЫ I2C (PA8 – SCL, PC9 – SDA)
//-------------------------------------------------------------
// Линии управляются по принципу открытого стока: в ODR
// записан 0, и уровень 0 формируется переводом вывода
// в режим выхода, а уровень 1 – переводом во вход
// (линия отпускается и подтягивается к +3,3 В
// резисторами R1, R2). Состояние линий читается через IDR.
// Частота шины F_SCL = 200 кГц: каждый бит – два
// полупериода по 2,5 мкс (низкий и высокий уровень SCL)
//=============================================================
// тактов ядра на один проход цикла sw_wait() (-O0)
#define SW_LOOP_CYCLES    10UL
// предельное время удержания SCL ведомым, проходов цикла
#define SW_STRETCH_MAX    1000UL

#define SCL_L()   SET_BIT(GPIOA->MODER, GPIO_MODER_MODER8_0)
#define SCL_REL() CLEAR_BIT(GPIOA->MODER, GPIO_MODER_MODER8_Msk)
#define SDA_L()   SET_BIT(GPIOC->MODER, GPIO_MODER_MODER9_0)
#define SDA_H()   CLEAR_BIT(GPIOC->MODER, GPIO_MODER_MODER9_Msk)
#define SCL_READ() ((GPIOA->IDR & GPIO_IDR_ID8) ? 1U : 0U)
#define SDA_READ() ((GPIOC->IDR & GPIO_IDR_ID9) ? 1U : 0U)

// число проходов цикла задержки на полупериод SCL
static uint32_t sw_ticks = 1;

// отпускание SCL и ожидание его фактического подъёма:
// ведомый вправе удерживать SCL (clock stretching)
static void SCL_H(void)
{
    uint32_t t = SW_STRETCH_MAX;

    SCL_REL();
    while (!SCL_READ() && --t) {};
}

// задержка на полупериод SCL (2,5 мкс при 200 кГц)
static void sw_wait(void)
{
    volatile uint32_t n = sw_ticks;
    while (n--) {};
}

//-------------------------------------------------------------
// Перевод PA8, PC9 в режим выхода с открытым стоком
//-------------------------------------------------------------
void SW_I2C_Init(void)
{
    I2C3->CR1 &= ~I2C_CR1_PE;     // модуль I2C3 отключён

    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIOAEN);
    SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIOCEN);

    // исходно обе линии отпущены: выводы – входы
    // без внутренней подтяжки (подтяжка внешняя)
    SCL_REL();
    SDA_H();
    CLEAR_BIT(GPIOA->PUPDR, GPIO_PUPDR_PUPD8_Msk);
    CLEAR_BIT(GPIOC->PUPDR, GPIO_PUPDR_PUPD9_Msk);
    // в режиме выхода вывод выдаёт только 0
    GPIOA->BSRR = GPIO_BSRR_BR8;
    GPIOC->BSRR = GPIO_BSRR_BR9;

    // полупериод в тактах ядра: 84 МГц / (2 * 200 кГц) = 210
    // (значение уточнено по осциллограмме в Proteus)
    sw_ticks = SystemCoreClock / (2UL * F_SCL) / SW_LOOP_CYCLES;
    sw_wait();
}

//-------------------------------------------------------------
// START (и повторный START): спад SDA при высоком SCL
//-------------------------------------------------------------
static void sw_start(void)
{
    SDA_H(); sw_wait();
    SCL_H(); sw_wait();
    SDA_L(); sw_wait();
    SCL_L();
}

//-------------------------------------------------------------
// STOP: фронт SDA при высоком SCL
//-------------------------------------------------------------
static void sw_stop(void)
{
    SDA_L(); sw_wait();
    SCL_H(); sw_wait();
    SDA_H(); sw_wait();
}

//-------------------------------------------------------------
// Передача байта старшим битом вперёд.
// Возвращает 1, если ведомый ответил ACK, иначе 0
//-------------------------------------------------------------
static uint8_t sw_write(uint8_t b)
{
    uint8_t i, ack;

    for (i = 0; i < 8; i++) {
        if (b & 0x80) SDA_H();    // SDA меняется при SCL = 0
        else          SDA_L();
        b <<= 1;
        sw_wait();
        SCL_H(); sw_wait();       // ведомый читает бит
        SCL_L();
    }
    SDA_H();                      // линия отдана ведомому
    sw_wait();
    SCL_H(); sw_wait();           // 9-й такт – ответ
    ack = (uint8_t)(SDA_READ() == 0U);
    SCL_L();
    return ack;
}

//-------------------------------------------------------------
// Приём байта; ack = 1 – ответить ACK, 0 – NACK
//-------------------------------------------------------------
static uint8_t sw_read(uint8_t ack)
{
    uint8_t i, b = 0;

    SDA_H();                      // линия отдана ведомому
    for (i = 0; i < 8; i++) {
        sw_wait();
        SCL_H(); sw_wait();
        b = (uint8_t)((b << 1) | SDA_READ());
        SCL_L();
    }
    if (ack) SDA_L();             // ответ ведущего
    else     SDA_H();
    sw_wait();
    SCL_H(); sw_wait();
    SCL_L();
    SDA_H();
    return b;
}

//-------------------------------------------------------------
// Чтение байта из регистра:
// S – адрес+W – номер регистра – Sr – адрес+R – байт – NACK – P
// Возвращает байт либо 0xFF при ошибке
//-------------------------------------------------------------
uint8_t SW_ReadByte(uint8_t dev_addr, uint8_t reg)
{
    uint8_t data;

    sw_start();
    i2c_step = 21;
    if (!sw_write((uint8_t)((dev_addr << 1) & 0xFE))) goto error;
    i2c_step = 22;
    if (!sw_write(reg)) goto error;
    sw_start();                   // повторный старт
    i2c_step = 23;
    if (!sw_write((uint8_t)((dev_addr << 1) | 0x01))) goto error;
    data = sw_read(0);            // единственный байт – NACK
    sw_stop();
    i2c_step = 0;
    return data;

error:
    sw_stop();
    return 0xFF;
}

//-------------------------------------------------------------
// Запись байта в регистр:
// S – адрес+W – номер регистра – значение – P
// Возвращает 0 при успехе, 0xFF при ошибке
//-------------------------------------------------------------
uint8_t SW_WriteByte(uint8_t dev_addr, uint8_t reg,
                     uint8_t value)
{
    sw_start();
    i2c_step = 31;
    if (!sw_write((uint8_t)((dev_addr << 1) & 0xFE))) goto error;
    i2c_step = 32;
    if (!sw_write(reg)) goto error;
    i2c_step = 33;
    if (!sw_write(value)) goto error;
    sw_stop();
    i2c_step = 0;
    return 0;

error:
    sw_stop();
    return 0xFF;
}
