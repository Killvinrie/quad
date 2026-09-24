#include "main.h"

/* STM32F103C8 reset clock: internal HSI at 8 MHz. GPIO, USART, SPI and ADC
 * registers are configured here instead of relying on generated CubeMX code. */
#define REG32(addr) (*(volatile uint32_t *)(addr))
#define REG16(addr) (*(volatile uint16_t *)(addr))
#define RCC_APB2ENR REG32(0x40021018UL)
#define RCC_APB1ENR REG32(0x4002101cUL)
#define RCC_CFGR    REG32(0x40021004UL)
#define AFIO_MAPR   REG32(0x40010004UL)
#define SYST_CSR    REG32(0xe000e010UL)
#define SYST_RVR    REG32(0xe000e014UL)
#define SYST_CVR    REG32(0xe000e018UL)

#define ADC_SR      REG32(0x40012400UL)
#define ADC_CR2     REG32(0x40012408UL)
#define ADC_SMPR2   REG32(0x40012410UL)
#define ADC_SQR1    REG32(0x4001242cUL)
#define ADC_SQR3    REG32(0x40012434UL)
#define ADC_DR      REG32(0x4001244cUL)

#define SPI_CR1     REG32(0x40003800UL)
#define SPI_SR      REG32(0x40003808UL)
#define SPI_DR      REG16(0x4000380cUL)

#define USART_SR    REG32(0x40013800UL)
#define USART_DR    REG16(0x40013804UL)
#define USART_BRR   REG32(0x40013808UL)
#define USART_CR1   REG32(0x4001380cUL)

ADC_HandleTypeDef hadc1;
SPI_HandleTypeDef hspi2;
UART_HandleTypeDef huart1;
static volatile uint32_t millisecond_tick;

uint32_t HAL_GetTick(void) { return millisecond_tick; }
void SysTick_Handler(void) { ++millisecond_tick; }
void HAL_Delay(uint32_t ms)
{
    uint32_t start = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - start) < ms) { }
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    return (port->IDR & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    port->BSRR = state == GPIO_PIN_SET ? pin : (uint32_t)pin << 16;
}

/* F1 GPIO nibble: 0x0 analog, 0x4 floating input, 0x8 pull input,
 * 0x2 push-pull output 2MHz, 0x9 AF push-pull 10MHz. */
static void gpio_mode(GPIO_TypeDef *port, unsigned bit, uint32_t mode)
{
    volatile uint32_t *cr = bit < 8 ? &port->CRL : &port->CRH;
    unsigned shift = (bit & 7U) * 4U;
    *cr = (*cr & ~(15UL << shift)) | (mode << shift);
}
static void pullup(GPIO_TypeDef *port, unsigned bit)
{
    port->BSRR = 1UL << bit;
    gpio_mode(port, bit, 8U);
}

void Board_Init(void)
{
    /* AFIO + GPIOA/B/C + ADC1 + USART1 on APB2; SPI2 on APB1. */
    RCC_APB2ENR |= (1U << 0) | (1U << 2) | (1U << 3) | (1U << 4) |
                    (1U << 9) | (1U << 14);
    RCC_APB1ENR |= 1U << 14;
    /* Disable JTAG while preserving SWD, releasing PA15 for KEY1. */
    AFIO_MAPR = (AFIO_MAPR & ~(7UL << 24)) | (2UL << 24);
    /* ADC clock PCLK2/2 = 4MHz; F1 ADC maximum is 14MHz. */
    RCC_CFGR &= ~(3UL << 14);

    SYST_RVR = 8000U - 1U;
    SYST_CVR = 0;
    SYST_CSR = 7U; /* core clock, interrupt, enable */

    for (unsigned bit = 0; bit < 4; ++bit) gpio_mode(GPIOA, bit, 0U);
    pullup(GPIOA, 15); pullup(GPIOB, 11);
    pullup(GPIOC, 14); pullup(GPIOC, 15);
    pullup(GPIOA, 6); pullup(GPIOA, 7);
    pullup(GPIOB, 0); pullup(GPIOB, 1);

    /* NRF24: CE PA11 low, CSN PB12 high, SPI2 PB13/14/15. */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
    gpio_mode(GPIOA, 11, 2U);
    gpio_mode(GPIOB, 12, 2U);
    gpio_mode(GPIOB, 13, 9U); /* SCK */
    gpio_mode(GPIOB, 14, 4U); /* MISO */
    gpio_mode(GPIOB, 15, 9U); /* MOSI */
    gpio_mode(GPIOA, 12, 2U); /* buzzer reserved, initially low */

    /* USART1 PA9 TX, PA10 RX; 8N1, 115200 at 8MHz. */
    gpio_mode(GPIOA, 9, 9U);
    gpio_mode(GPIOA, 10, 4U);
    USART_BRR = (8000000U + 57600U) / 115200U;
    USART_CR1 = (1U << 13) | (1U << 3) | (1U << 2);

    /* SPI2 master, mode 0, software NSS, 8MHz/4 = 2MHz. */
    SPI_CR1 = (1U << 9) | (1U << 8) | (1U << 6) |
              (1U << 3) | (1U << 2);

    ADC_SQR1 = 0;
    ADC_CR2 = (7U << 17) | (1U << 20) | 1U; /* software trigger, ADON */
    HAL_Delay(1);
    ADC_CR2 |= 1U << 3; /* reset calibration */
    while (ADC_CR2 & (1U << 3)) { }
    ADC_CR2 |= 1U << 2; /* calibration */
    while (ADC_CR2 & (1U << 2)) { }

}

HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *adc,
                                        const ADC_ChannelConfTypeDef *config)
{
    (void)adc;
    if (!config || config->Channel > 3U ||
        config->Rank != ADC_REGULAR_RANK_1) return HAL_ERROR;
    unsigned shift = config->Channel * 3U;
    ADC_SMPR2 = (ADC_SMPR2 & ~(7UL << shift)) |
                ((config->SamplingTime & 7U) << shift);
    ADC_SQR3 = config->Channel;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc)
{
    (void)adc;
    ADC_SR &= ~(1U << 1);
    ADC_CR2 |= 1U << 22; /* SWSTART */
    return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc,
                                            uint32_t timeout)
{
    (void)adc;
    uint32_t start = HAL_GetTick();
    while (!(ADC_SR & (1U << 1)))
        if ((uint32_t)(HAL_GetTick() - start) >= timeout) return HAL_TIMEOUT;
    return HAL_OK;
}
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc)
{
    (void)adc;
    return ADC_DR & 0x0fffU;
}
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc)
{
    (void)adc;
    return HAL_OK; /* ADON remains set so the next conversion is immediate. */
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi,
                                          uint8_t *tx, uint8_t *rx,
                                          uint16_t length, uint32_t timeout)
{
    (void)spi;
    uint32_t start = HAL_GetTick();
    for (uint16_t i = 0; i < length; ++i) {
        while (!(SPI_SR & (1U << 1)))
            if ((uint32_t)(HAL_GetTick() - start) >= timeout) return HAL_TIMEOUT;
        SPI_DR = tx[i];
        while (!(SPI_SR & 1U))
            if ((uint32_t)(HAL_GetTick() - start) >= timeout) return HAL_TIMEOUT;
        uint8_t received = (uint8_t)SPI_DR;
        if (rx) rx[i] = received;
    }
    while (SPI_SR & (1U << 7))
        if ((uint32_t)(HAL_GetTick() - start) >= timeout) return HAL_TIMEOUT;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi, uint8_t *data,
                                   uint16_t length, uint32_t timeout)
{
    return HAL_SPI_TransmitReceive(spi, data, 0, length, timeout);
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                                    uint16_t length, uint32_t timeout)
{
    (void)uart;
    uint32_t start = HAL_GetTick();
    for (uint16_t i = 0; i < length; ++i) {
        while (!(USART_SR & (1U << 7)))
            if ((uint32_t)(HAL_GetTick() - start) >= timeout) return HAL_TIMEOUT;
        USART_DR = data[i];
    }
    while (!(USART_SR & (1U << 6)))
        if ((uint32_t)(HAL_GetTick() - start) >= timeout) return HAL_TIMEOUT;
    return HAL_OK;
}
