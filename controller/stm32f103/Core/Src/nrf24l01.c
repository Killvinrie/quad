#include "nrf24l01.h"

#define NRF_CMD_R_REGISTER 0x00U
#define NRF_CMD_W_REGISTER 0x20U
#define NRF_CMD_R_RX_PAYLOAD 0x61U
#define NRF_CMD_W_TX_PAYLOAD 0xa0U
#define NRF_CMD_FLUSH_TX 0xe1U
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
#define NRF_REG_TX_ADDR 0x10U
#define NRF_REG_RX_PW_P0 0x11U
#define NRF_REG_DYNPD 0x1cU
#define NRF_REG_FEATURE 0x1dU

#define NRF_STATUS_RX_DR 0x40U
#define NRF_STATUS_TX_DS 0x20U
#define NRF_STATUS_MAX_RT 0x10U

static void csn(Nrf24 *r, GPIO_PinState state)
{
    HAL_GPIO_WritePin(r->csn_port, r->csn_pin, state);
}
static void ce(Nrf24 *r, GPIO_PinState state)
{
    HAL_GPIO_WritePin(r->ce_port, r->ce_pin, state);
}
static uint8_t command(Nrf24 *r, uint8_t cmd)
{
    uint8_t status = 0;
    csn(r, GPIO_PIN_RESET);
    (void)HAL_SPI_TransmitReceive(r->spi, &cmd, &status, 1, 10);
    csn(r, GPIO_PIN_SET);
    return status;
}
static void write_reg(Nrf24 *r, uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {(uint8_t)(NRF_CMD_W_REGISTER | reg), value};
    csn(r, GPIO_PIN_RESET);
    (void)HAL_SPI_Transmit(r->spi, tx, sizeof(tx), 10);
    csn(r, GPIO_PIN_SET);
}
static void write_buf(Nrf24 *r, uint8_t reg, const uint8_t *data, uint8_t len)
{
    uint8_t cmd = (uint8_t)(NRF_CMD_W_REGISTER | reg);
    csn(r, GPIO_PIN_RESET);
    (void)HAL_SPI_Transmit(r->spi, &cmd, 1, 10);
    (void)HAL_SPI_Transmit(r->spi, (uint8_t *)data, len, 10);
    csn(r, GPIO_PIN_SET);
}
static void tx_payload(Nrf24 *r, const uint8_t *data, uint8_t len)
{
    uint8_t cmd = NRF_CMD_W_TX_PAYLOAD;
    csn(r, GPIO_PIN_RESET);
    (void)HAL_SPI_Transmit(r->spi, &cmd, 1, 10);
    (void)HAL_SPI_Transmit(r->spi, (uint8_t *)data, len, 10);
    csn(r, GPIO_PIN_SET);
}

int Nrf24_TxInit(Nrf24 *r)
{
    static const uint8_t address[5] = {'Q', 'D', 'R', 'C', '1'};
    ce(r, GPIO_PIN_RESET);
    csn(r, GPIO_PIN_SET);
    HAL_Delay(5);
    write_reg(r, NRF_REG_CONFIG, 0x0c); /* CRC-2, powered down, PTX. */
    write_reg(r, NRF_REG_EN_AA, 0x01);  /* ACK on pipe 0. */
    write_reg(r, NRF_REG_EN_RXADDR, 0x01);
    write_reg(r, NRF_REG_SETUP_AW, 0x03); /* 5-byte address. */
    write_reg(r, NRF_REG_SETUP_RETR, 0x13); /* 3 retries, 1.5ms delay. */
    write_reg(r, NRF_REG_RF_CH, 76);
    write_reg(r, NRF_REG_RF_SETUP, 0x06); /* 1Mbps, 0dBm. */
    write_buf(r, NRF_REG_TX_ADDR, address, sizeof(address));
    write_buf(r, NRF_REG_RX_ADDR_P0, address, sizeof(address));
    write_reg(r, NRF_REG_RX_PW_P0, 20);
    write_reg(r, NRF_REG_DYNPD, 0);
    write_reg(r, NRF_REG_FEATURE, 0);
    write_reg(r, NRF_REG_STATUS, NRF_STATUS_RX_DR | NRF_STATUS_TX_DS |
                                   NRF_STATUS_MAX_RT);
    (void)command(r, NRF_CMD_FLUSH_TX);
    write_reg(r, NRF_REG_CONFIG, 0x0e); /* CRC-2, power up, PTX. */
    HAL_Delay(2);
    return 1;
}

int Nrf24_Send(Nrf24 *r, const uint8_t *payload, uint8_t length)
{
    uint32_t start;
    uint8_t status;
    if (!payload || length == 0 || length > 32) return 0;
    write_reg(r, NRF_REG_STATUS, NRF_STATUS_RX_DR | NRF_STATUS_TX_DS |
                                   NRF_STATUS_MAX_RT);
    (void)command(r, NRF_CMD_FLUSH_TX);
    tx_payload(r, payload, length);
    ce(r, GPIO_PIN_SET);
    HAL_Delay(2); /* Full millisecond minimum, exceeding 10 us PTX pulse. */
    ce(r, GPIO_PIN_RESET);
    start = HAL_GetTick();
    do {
        status = command(r, NRF_CMD_NOP);
        if (status & NRF_STATUS_TX_DS) {
            ce(r, GPIO_PIN_RESET);
            write_reg(r, NRF_REG_STATUS, NRF_STATUS_TX_DS);
            return 1;
        }
        if (status & NRF_STATUS_MAX_RT) break;
    } while ((uint32_t)(HAL_GetTick() - start) < 10U);
    ce(r, GPIO_PIN_RESET);
    write_reg(r, NRF_REG_STATUS, NRF_STATUS_MAX_RT);
    (void)command(r, NRF_CMD_FLUSH_TX);
    return 0;
}
