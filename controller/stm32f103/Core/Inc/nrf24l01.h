#ifndef CONTROLLER_NRF24L01_H
#define CONTROLLER_NRF24L01_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef struct {
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *ce_port;
    uint16_t ce_pin;
    GPIO_TypeDef *csn_port;
    uint16_t csn_pin;
} Nrf24;

int Nrf24_TxInit(Nrf24 *radio);
int Nrf24_Send(Nrf24 *radio, const uint8_t *payload, uint8_t length);

#endif
