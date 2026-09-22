#include "sensor_app.h"
#include "sensor_config.h"
#include "main.h"
#include "gps_nmea.h"
#include "bmp388_math.h"
#include "../../Common/display_link.h"
#include <stdio.h>
#include <string.h>

extern ADC_HandleTypeDef hadc1;
static I2C_HandleTypeDef imu_bus, baro_bus;
static UART_HandleTypeDef gps_uart, link_uart;
static uint16_t imu_address, baro_address;
static uint8_t imu_ready, baro_ready, imu_valid, baro_valid, adc_valid;
static int32_t acc[3], gyro[3], imu_temp, baro_temp, pressure;
static uint32_t imu_tick, baro_tick, adc_mv, adc_raw;
static BmpCalibration calibration;
static GpsFix gps;
static uint8_t gps_seen;
static uint32_t gps_tick;
/* ISR producer/main consumer; overflow discards buffered partial sentences. */
#define GPS_RING_SIZE 1024U
static volatile uint8_t gps_ring[GPS_RING_SIZE];
static volatile uint16_t gps_head, gps_tail;
static volatile uint8_t gps_overflow;
static char gps_line[128];
static unsigned gps_used;
static unsigned display_page;
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState raw, stable;
    uint32_t changed_at;
} PageKey;
/* PCB buttons have external pull-ups: pressed = low. */
static PageKey page_keys[] = {
    {KEY1_GPIO_Port, KEY1_Pin, GPIO_PIN_SET, GPIO_PIN_SET, 0},
    {KEY2_GPIO_Port, KEY2_Pin, GPIO_PIN_SET, GPIO_PIN_SET, 0},
    {KEY3_GPIO_Port, KEY3_Pin, GPIO_PIN_SET, GPIO_PIN_SET, 0}
};
static void init_page_keys(void)
{
    display_page = 0;
    for (unsigned i = 0; i < 3; ++i) {
        page_keys[i].raw = HAL_GPIO_ReadPin(page_keys[i].port, page_keys[i].pin);
        page_keys[i].stable = page_keys[i].raw;
        page_keys[i].changed_at = HAL_GetTick();
    }
}
static void poll_page_keys(uint32_t now)
{
    unsigned pressed = 0;
    for (unsigned i = 0; i < 3; ++i) {
        PageKey *key = &page_keys[i];
        GPIO_PinState raw = HAL_GPIO_ReadPin(key->port, key->pin);
        if (raw != key->raw) {
            key->raw = raw;
            key->changed_at = now;
        }
        if (key->stable != key->raw &&
            (uint32_t)(now - key->changed_at) >= 30U) {
            key->stable = key->raw;
            if (key->stable == GPIO_PIN_RESET) pressed |= 1U << i;
        }
    }
    /* One event per press, no hold repeat. Home wins simultaneous presses. */
    if (pressed & 4U) display_page = 0;
    else if (pressed == 1U) display_page = (display_page + 1U) % 3U;
    else if (pressed == 2U) display_page = (display_page + 2U) % 3U;
}

static int read_reg(I2C_HandleTypeDef *bus, uint16_t addr, uint8_t reg,
                    uint8_t *data, uint16_t size)
{
    return HAL_I2C_Mem_Read(bus, addr, reg, I2C_MEMADD_SIZE_8BIT, data, size,
                            SENSOR_IO_TIMEOUT_MS) == HAL_OK;
}
static int write_reg(I2C_HandleTypeDef *bus, uint16_t addr, uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(bus, addr, reg, I2C_MEMADD_SIZE_8BIT, &value, 1,
                             SENSOR_IO_TIMEOUT_MS) == HAL_OK;
}
static uint16_t probe(I2C_HandleTypeDef *bus, uint8_t first, uint8_t reg, uint8_t id)
{
    uint8_t value;
    for (unsigned i = 0; i < 2; ++i) {
        uint16_t addr = (uint16_t)((first + i) << 1);
        if (read_reg(bus, addr, reg, &value, 1) && value == id) return addr;
    }
    return 0;
}
static void init_imu(void)
{
    imu_valid = imu_ready = 0;
    imu_address = probe(&imu_bus, 0x68, 0x75, 0x68);
    if (!imu_address || !write_reg(&imu_bus, imu_address, 0x6b, 0x80)) return;
    HAL_Delay(100);
    /* PLL clock, all axes, DLPF 44/42 Hz, 50 Hz sample, +/-2g, +/-250 dps. */
    imu_ready = write_reg(&imu_bus, imu_address, 0x6b, 1) &&
                write_reg(&imu_bus, imu_address, 0x6c, 0) &&
                write_reg(&imu_bus, imu_address, 0x1a, 3) &&
                write_reg(&imu_bus, imu_address, 0x19, 19) &&
                write_reg(&imu_bus, imu_address, 0x1b, 0) &&
                write_reg(&imu_bus, imu_address, 0x1c, 0);
    imu_tick = HAL_GetTick();
}
static void init_baro(void)
{
    uint8_t raw[21], status;
    baro_valid = baro_ready = 0;
    baro_address = probe(&baro_bus, 0x76, 0, 0x50);
    if (!baro_address ||
        !read_reg(&baro_bus, baro_address, 3, &status, 1) || !(status & 0x10) ||
        !write_reg(&baro_bus, baro_address, 0x7e, 0xb6)) return;
    HAL_Delay(10);
    if (!read_reg(&baro_bus, baro_address, 0x31, raw, sizeof(raw))) return;
    unsigned zeros = 0, ones = 0;
    for (unsigned i = 0; i < sizeof(raw); ++i) { zeros += raw[i] == 0; ones += raw[i] == 255; }
    if (zeros == sizeof(raw) || ones == sizeof(raw)) return;
    Bmp388_DecodeCalibration(raw, &calibration);
    /* Pressure x8, temperature x1, 25 Hz, IIR coefficient 3, normal mode. */
    baro_ready = write_reg(&baro_bus, baro_address, 0x1c, 3) &&
                 write_reg(&baro_bus, baro_address, 0x1d, 3) &&
                 write_reg(&baro_bus, baro_address, 0x1f, 4) &&
                 write_reg(&baro_bus, baro_address, 0x1b, 0x33);
    HAL_Delay(5);
    if (!read_reg(&baro_bus, baro_address, 2, &status, 1) || (status & 7))
        baro_ready = 0;
    baro_tick = HAL_GetTick();
}
static int32_t be16(const uint8_t *p)
{
    int32_t n = ((uint32_t)p[0] << 8) | p[1];
    return n >= 32768 ? n - 65536 : n;
}
static uint32_t le24(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}
static void sample_sensors(void)
{
    uint8_t raw[14], status;
    if (imu_ready) {
        if (!read_reg(&imu_bus, imu_address, 0x3a, &status, 1)) {
            imu_valid = imu_ready = 0;
        } else if (status & 1) {
            if (!read_reg(&imu_bus, imu_address, 0x3b, raw, 14)) {
                imu_valid = imu_ready = 0;
            } else {
                for (unsigned i = 0; i < 3; ++i) {
                    acc[i] = be16(raw + 2 * i) * 1000 / 16384;
                    gyro[i] = be16(raw + 8 + 2 * i) * 100 / 131;
                }
                imu_temp = be16(raw + 6) * 100 / 340 + 3653;
                imu_tick = HAL_GetTick(); imu_valid = 1;
            }
        }
        if ((uint32_t)(HAL_GetTick() - imu_tick) > 500) imu_valid = imu_ready = 0;
    }
    if (baro_ready) {
        if (!read_reg(&baro_bus, baro_address, 3, &status, 1)) {
            baro_valid = baro_ready = 0;
        } else if ((status & 0x60) == 0x60) {
            if (!read_reg(&baro_bus, baro_address, 4, raw, 6) ||
                !Bmp388_Compensate(&calibration, le24(raw + 3), le24(raw),
                                  &baro_temp, &pressure)) {
                baro_valid = baro_ready = 0;
            } else {
                baro_tick = HAL_GetTick(); baro_valid = 1;
            }
        }
        if ((uint32_t)(HAL_GetTick() - baro_tick) > 500) baro_valid = baro_ready = 0;
    }
}
static void init_i2c(I2C_HandleTypeDef *h, I2C_TypeDef *instance)
{
    h->Instance = instance;
    h->Init.ClockSpeed = SENSOR_I2C_HZ;
    h->Init.DutyCycle = I2C_DUTYCYCLE_2;
    h->Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    h->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    h->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    h->Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(h) != HAL_OK) Error_Handler();
}
static void init_uart(UART_HandleTypeDef *h, USART_TypeDef *instance, uint32_t baud)
{
    h->Instance = instance;
    h->Init.BaudRate = baud;
    h->Init.WordLength = UART_WORDLENGTH_8B;
    h->Init.StopBits = UART_STOPBITS_1;
    h->Init.Parity = UART_PARITY_NONE;
    h->Init.Mode = UART_MODE_TX_RX;
    h->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    h->Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(h) != HAL_OK) Error_Handler();
}
void SensorApp_GpsIRQ(void)
{
    uint32_t status = USART1->SR;
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        uint8_t byte = (uint8_t)USART1->DR; /* SR then DR clears error flags. */
        if (status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
            gps_overflow = 1; return;
        }
        uint16_t next = (uint16_t)((gps_head + 1) % GPS_RING_SIZE);
        if (next == gps_tail) gps_overflow = 1;
        else { gps_ring[gps_head] = byte; gps_head = next; }
    }
}
static void poll_gps(void)
{
    if (gps_overflow) {
        __disable_irq();
        gps_tail = gps_head; gps_overflow = 0;
        __enable_irq();
        gps_used = 0; gps_seen = 0;
    }
    while (gps_tail != gps_head) {
        char ch = (char)gps_ring[gps_tail];
        gps_tail = (uint16_t)((gps_tail + 1) % GPS_RING_SIZE);
        if (ch == '$') { gps_used = 0; gps_line[gps_used++] = ch; }
        else if (gps_used && ch == '\n') {
            gps_line[gps_used] = 0;
            if (Gps_ParseGga(gps_line, &gps)) { gps_tick = HAL_GetTick(); gps_seen = 1; }
            gps_used = 0;
        } else if (gps_used) {
            if (gps_used < sizeof(gps_line) - 1) gps_line[gps_used++] = ch;
            else gps_used = 0;
        }
    }
}
/* Integer formatting keeps IAR/newlib float printf support unnecessary. */
static void fixed(char *out, size_t size, const char *label, int32_t value,
                  unsigned scale, unsigned decimals, const char *unit)
{
    unsigned long magnitude = (unsigned long)(value < 0 ? -value : value);
    snprintf(out, size, "%s%s%lu.%0*lu%s", label, value < 0 ? "-" : "",
             magnitude / scale, (int)decimals, magnitude % scale, unit);
}
static void send_screen(uint32_t now)
{
    char rows[DISPLAY_ROWS][DISPLAY_COLS + 1] = {{0}};
    uint8_t frame[DISPLAY_FRAME_SIZE];
    unsigned page = display_page;
    if (page == 0) {
        strcpy(rows[0], "1/3 MPU6050");
        if (imu_valid) {
            for (unsigned i = 0; i < 3; ++i) {
                const char *al[] = {"AX:", "AY:", "AZ:"};
                const char *gl[] = {"GX:", "GY:", "GZ:"};
                fixed(rows[1+i], sizeof(rows[0]), al[i], acc[i], 1000, 3, "G");
                fixed(rows[4+i], sizeof(rows[0]), gl[i], gyro[i], 100, 2, "D/S");
            }
            fixed(rows[7], sizeof(rows[0]), "TEMP:", imu_temp, 100, 2, "C");
        } else { strcpy(rows[2], "OFFLINE / WAIT DATA"); strcpy(rows[4], "CHECK I2C1 PB6/PB7"); }
    } else if (page == 1) {
        strcpy(rows[0], "2/3 BMP388 / ADC");
        if (baro_valid) {
            fixed(rows[2], sizeof(rows[0]), "P:", pressure, 100, 2, "HPA");
            fixed(rows[3], sizeof(rows[0]), "T:", baro_temp, 100, 2, "C");
        } else strcpy(rows[2], "BMP388 OFFLINE");
        if (adc_valid) {
            snprintf(rows[5], sizeof(rows[0]), "ADC RAW:%lu", (unsigned long)adc_raw);
            fixed(rows[6], sizeof(rows[0]), "PA1:", (int32_t)adc_mv, 1000, 3, "V");
        } else strcpy(rows[5], "ADC ERROR");
        strcpy(rows[7], "PIN VOLTAGE ONLY");
    } else {
        strcpy(rows[0], "3/3 GPS NMEA GGA");
        if (!gps_seen || (uint32_t)(now - gps_tick) > 3000) {
            strcpy(rows[2], "NO GGA / TIMEOUT");
            snprintf(rows[4], sizeof(rows[0]), "BAUD:%lu", (unsigned long)SENSOR_GPS_BAUD);
        } else {
            snprintf(rows[1], sizeof(rows[0]), "FIX:%u SAT:%u", gps.quality, gps.satellites);
            if (gps.quality) {
                strcpy(rows[2], "LAT DDMM.MMMM");
                snprintf(rows[3], sizeof(rows[0]), "%s %c", gps.latitude, gps.ns);
                strcpy(rows[4], "LON DDDMM.MMMM");
                snprintf(rows[5], sizeof(rows[0]), "%s %c", gps.longitude, gps.ew);
            } else strcpy(rows[3], "WAITING FOR FIX");
        }
    }
    display_encode(frame, rows);
    /* A failed frame is discarded; next refresh sends a complete CRC frame. */
    (void)HAL_UART_Transmit(&link_uart, frame, sizeof(frame), 30);
}
void SensorApp_Init(void)
{
    /* Pin alternate functions are already configured by MX_GPIO_Init. */
    __HAL_RCC_I2C1_CLK_ENABLE(); __HAL_RCC_I2C2_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE(); __HAL_RCC_USART2_CLK_ENABLE();
    init_i2c(&imu_bus, I2C1); init_i2c(&baro_bus, I2C2);
    init_uart(&gps_uart, USART1, SENSOR_GPS_BAUD);
    init_uart(&link_uart, USART2, SENSOR_LINK_BAUD);
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    __HAL_UART_ENABLE_IT(&gps_uart, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&gps_uart, UART_IT_ERR);
    HAL_Delay(100);
    init_imu(); init_baro();
    init_page_keys();
}
void SensorApp_Poll(void)
{
    static uint32_t sample_tick, screen_tick, retry_tick;
    uint32_t now = HAL_GetTick();
    poll_page_keys(now);
    poll_gps();
    if ((uint32_t)(now - sample_tick) >= 20) {
        sample_tick = now; sample_sensors();
    }
    if ((uint32_t)(now - retry_tick) >= SENSOR_RETRY_MS) {
        retry_tick = now;
        if (!imu_ready) init_imu();
        if (!baro_ready) init_baro();
    }
    now = HAL_GetTick();
    if ((uint32_t)(now - screen_tick) >= 200) {
        screen_tick = now; adc_valid = 0;
        if (HAL_ADC_Start(&hadc1) == HAL_OK && HAL_ADC_PollForConversion(&hadc1, 5) == HAL_OK) {
            adc_raw = HAL_ADC_GetValue(&hadc1);
            adc_mv = (adc_raw * SENSOR_ADC_VREF_MV + 2047) / 4095;
            adc_valid = 1;
        }
        (void)HAL_ADC_Stop(&hadc1);
        send_screen(now);
    }
    HAL_Delay(1);
}
