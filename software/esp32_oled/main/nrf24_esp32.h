#ifndef NRF24_ESP32_H
#define NRF24_ESP32_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t ce;
} Nrf24Esp;

esp_err_t nrf24_esp_init(Nrf24Esp *radio, int sck, int mosi, int miso,
                         int csn, int ce);
bool nrf24_esp_receive(Nrf24Esp *radio, uint8_t payload[32], uint8_t length);

#endif
