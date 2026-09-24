#ifndef CONTROLLER_STM32F1XX_HAL_H
#define CONTROLLER_STM32F1XX_HAL_H

/* Small, self-contained HAL-compatible subset for this F103C8 controller.
 * No CubeMX output or external STM32 HAL package is required. */
#include <stdint.h>

typedef struct {
    volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
} GPIO_TypeDef;

#define GPIOA ((GPIO_TypeDef *)0x40010800UL)
#define GPIOB ((GPIO_TypeDef *)0x40010c00UL)
#define GPIOC ((GPIO_TypeDef *)0x40011000UL)
#define GPIO_PIN_0  (1U << 0)
#define GPIO_PIN_1  (1U << 1)
#define GPIO_PIN_6  (1U << 6)
#define GPIO_PIN_7  (1U << 7)
#define GPIO_PIN_11 (1U << 11)
#define GPIO_PIN_12 (1U << 12)
#define GPIO_PIN_13 (1U << 13)
#define GPIO_PIN_14 (1U << 14)
#define GPIO_PIN_15 (1U << 15)

typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_TIMEOUT = 3 } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;
typedef struct { uint32_t unused; } ADC_HandleTypeDef;
typedef struct { uint32_t unused; } SPI_HandleTypeDef;
typedef struct { uint32_t unused; } UART_HandleTypeDef;
typedef struct {
    uint32_t Channel;
    uint32_t Rank;
    uint32_t SamplingTime;
} ADC_ChannelConfTypeDef;

#define ADC_CHANNEL_0 0U
#define ADC_CHANNEL_1 1U
#define ADC_CHANNEL_2 2U
#define ADC_CHANNEL_3 3U
#define ADC_REGULAR_RANK_1 1U
#define ADC_SAMPLETIME_239CYCLES_5 7U

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t ms);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *adc,
                                        const ADC_ChannelConfTypeDef *config);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi, uint8_t *data,
                                   uint16_t length, uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi,
                                          uint8_t *tx, uint8_t *rx,
                                          uint16_t length, uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                                    uint16_t length, uint32_t timeout);

void Board_Init(void);
void SysTick_Handler(void);

#endif
