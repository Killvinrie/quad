#include "nrf24_esp32.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define NRF_CMD_R_REGISTER 0x00U
#define NRF_CMD_W_REGISTER 0x20U
#define NRF_CMD_R_RX_PAYLOAD 0x61U
#define NRF_CMD_FLUSH_RX 0xe2U
#define NRF_CMD_NOP 0xffU

#define NRF_REG_CONFIG 0x00U
#define NRF_REG_EN_AA 0x01U
#define NRF_REG_EN_RXADDR 0x02U
#define NRF_REG_SETUP_AW 0x03U
#define NRF_REG_SETUP_RETR 0x04U
#define NRF_REG_RF_CH 0x05U
#define NRF_REG_RF_SETUP 0x06U
#define NRF_REG_STATUS 0x07U
#define NRF_REG_RX_ADDR_P0 0x0aU
#define NRF_REG_RX_PW_P0 0x11U
#define NRF_REG_FIFO_STATUS 0x17U
#define NRF_REG_DYNPD 0x1cU
#define NRF_REG_FEATURE 0x1dU

#define NRF_STATUS_RX_DR 0x40U
#define NRF_STATUS_TX_DS 0x20U
#define NRF_STATUS_MAX_RT 0x10U
#define NRF_FIFO_RX_EMPTY 0x01U

static esp_err_t transfer(Nrf24Esp *r, const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transaction_t transaction = {0};
    transaction.length = len * 8U;
    transaction.tx_buffer = tx;
    transaction.rx_buffer = rx;
    return spi_device_polling_transmit(r->spi, &transaction);
}

static uint8_t command(Nrf24Esp *r, uint8_t cmd)
{
    uint8_t tx[1] = {cmd}, rx[1] = {0};
    if (transfer(r, tx, rx, 1) != ESP_OK) return 0;
    return rx[0];
}

static void write_reg(Nrf24Esp *r, uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {(uint8_t)(NRF_CMD_W_REGISTER | reg), value};
    (void)transfer(r, tx, NULL, sizeof(tx));
}

static uint8_t read_reg(Nrf24Esp *r, uint8_t reg)
{
    uint8_t tx[2] = {(uint8_t)(NRF_CMD_R_REGISTER | reg), 0};
    uint8_t rx[2] = {0};
    if (transfer(r, tx, rx, sizeof(tx)) != ESP_OK) return 0;
    return rx[1];
}

static void write_buf(Nrf24Esp *r, uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t tx[6] = {(uint8_t)(NRF_CMD_W_REGISTER | reg), 0};
    if (len > sizeof(tx) - 1U) return;
    memcpy(tx + 1, data, len);
    (void)transfer(r, tx, NULL, len + 1U);
}

esp_err_t nrf24_esp_init(Nrf24Esp *r, int sck, int mosi, int miso,
                         int csn, int ce)
{
    static const uint8_t address[5] = {'Q', 'D', 'R', 'C', '1'};
    spi_bus_config_t bus = {
        .mosi_io_num = mosi, .miso_io_num = miso, .sclk_io_num = sck,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 64
    };
    spi_device_interface_config_t device = {
        .clock_speed_hz = 8000000, .mode = 0, .spics_io_num = csn,
        .queue_size = 1
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = spi_bus_add_device(SPI2_HOST, &device, &r->spi);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    r->ce = (gpio_num_t)ce;
    gpio_set_direction(r->ce, GPIO_MODE_OUTPUT);
    gpio_set_level(r->ce, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    write_reg(r, NRF_REG_CONFIG, 0x0f); /* CRC-2, power up, primary RX. */
    write_reg(r, NRF_REG_EN_AA, 0x01);
    write_reg(r, NRF_REG_EN_RXADDR, 0x01);
    write_reg(r, NRF_REG_SETUP_AW, 0x03);
    write_reg(r, NRF_REG_SETUP_RETR, 0x13);
    write_reg(r, NRF_REG_RF_CH, 76);
    write_reg(r, NRF_REG_RF_SETUP, 0x06);
    write_buf(r, NRF_REG_RX_ADDR_P0, address, sizeof(address));
    write_reg(r, NRF_REG_RX_PW_P0, 20);
    write_reg(r, NRF_REG_DYNPD, 0);
    write_reg(r, NRF_REG_FEATURE, 0);
    write_reg(r, NRF_REG_STATUS, NRF_STATUS_RX_DR | NRF_STATUS_TX_DS |
                                   NRF_STATUS_MAX_RT);
    (void)command(r, NRF_CMD_FLUSH_RX);
    gpio_set_level(r->ce, 1);
    return ESP_OK;
}

bool nrf24_esp_receive(Nrf24Esp *r, uint8_t payload[32], uint8_t length)
{
    uint8_t tx[33] = {0}, rx[33] = {0};
    if (!r || !payload || length == 0 || length > 32) return false;
    if (read_reg(r, NRF_REG_FIFO_STATUS) & NRF_FIFO_RX_EMPTY) return false;
    tx[0] = NRF_CMD_R_RX_PAYLOAD;
    if (transfer(r, tx, rx, (size_t)length + 1U) != ESP_OK) return false;
    memcpy(payload, rx + 1, length);
    write_reg(r, NRF_REG_STATUS, NRF_STATUS_RX_DR);
    return true;
}
