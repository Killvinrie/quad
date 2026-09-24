#include <stdint.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "display_link.h"
#include "control_link.h"
#include "phone_control.h"
#include "phone_server.h"

static const char *TAG = "quad_oled";
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t oled;
static DisplayParser parser;
static PhoneControl phone_control;
static char sensor_screen[DISPLAY_ROWS][DISPLAY_COLS + 1];
static char phone_screen[DISPLAY_ROWS][DISPLAY_COLS + 1];
/* 5 columns, bit 0 at top, one blank column between characters. */
static const uint8_t digits[10][5] = {
    {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
    {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},
    {0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e}
};
static const uint8_t letters[26][5] = {
    {0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
    {0x3e,0x41,0x49,0x49,0x7a},{0x7f,8,8,8,0x7f},
    {0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,1},
    {0x7f,8,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
    {0x7f,2,0x0c,2,0x7f},{0x7f,4,8,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e},{0x7f,9,9,9,6},
    {0x3e,0x41,0x51,0x21,0x5e},{0x7f,9,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},
    {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x38,0x40,0x3f},{0x63,0x14,8,0x14,0x63},
    {7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}
};
static uint8_t glyph(char ch, unsigned col)
{
    if (ch >= '0' && ch <= '9') return digits[ch - '0'][col];
    if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'][col];
    if (ch == '-') return 8;
    if (ch == '.') return col == 2 ? 0x60 : 0;
    if (ch == ':') return col == 2 ? 0x36 : 0;
    if (ch == '/') return (uint8_t)(0x20 >> col);
    if (ch == '+') return col == 2 ? 0x3e : 8;
    return 0;
}
static esp_err_t draw_screen(char rows[DISPLAY_ROWS][DISPLAY_COLS + 1])
{
    for (unsigned row = 0; row < DISPLAY_ROWS; ++row) {
        uint8_t command[] = {0, (uint8_t)(0xb0 + row), 0, 0x10};
        uint8_t pixels[129] = {0x40};
        for (unsigned col = 0; col < DISPLAY_COLS; ++col)
            for (unsigned x = 0; x < 5; ++x)
                pixels[1 + col * 6 + x] = glyph(rows[row][col], x);
        esp_err_t err = i2c_master_transmit(oled, command, sizeof(command), 50);
        if (err != ESP_OK) return err;
        err = i2c_master_transmit(oled, pixels, sizeof(pixels), 50);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
static int start_oled(void)
{
    if (oled) {
        (void)i2c_master_bus_rm_device(oled);
        oled = NULL;
    }
    for (uint16_t addr = 0x3c; addr <= 0x3d; ++addr) {
        if (i2c_master_probe(bus, addr, 25) != ESP_OK) continue;
        i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr, .scl_speed_hz = 100000
        };
        if (i2c_master_bus_add_device(bus, &config, &oled) != ESP_OK) return 0;
        /* SSD1306 128x64, internal charge pump, page addressing mode. */
        const uint8_t commands[] = {
            0,0xae,0xd5,0x80,0xa8,0x3f,0xd3,0,0x40,0x8d,0x14,
            0x20,2,0xa1,0xc8,0xda,0x12,0x81,0x7f,0xd9,0xf1,
            0xdb,0x40,0xa4,0xa6,0x2e,0xaf
        };
        if (i2c_master_transmit(oled, commands, sizeof(commands), 50) == ESP_OK) {
            ESP_LOGI(TAG, "SSD1306 at 0x%02x", addr);
            return 1;
        }
        (void)i2c_master_bus_rm_device(oled); oled = NULL;
    }
    return 0;
}
void app_main(void)
{
    i2c_master_bus_config_t i2c_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CONFIG_QUAD_OLED_SDA,
        .scl_io_num = CONFIG_QUAD_OLED_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_config, &bus));
    uart_config_t uart_config = {
        .baud_rate = CONFIG_QUAD_LINK_BAUD, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 4096, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, CONFIG_QUAD_LINK_TX, CONFIG_QUAD_LINK_RX,
                                UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    QueueHandle_t phone_queue = xQueueCreate(1, sizeof(PhoneInput));
    ESP_ERROR_CHECK(phone_queue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(PhoneServer_Start(phone_queue));
    strcpy(sensor_screen[0], "QUAD SENSOR MONITOR");
    strcpy(sensor_screen[3], "WAIT STM32 DATA");
    int online = start_oled(), received = 0, stale = 0, dirty = 1;
    TickType_t last_rx = xTaskGetTickCount(), last_retry = last_rx, last_draw = last_rx;
    const uint32_t control_timeout = pdMS_TO_TICKS(250);
    int phone_was_online = 0;
    for (;;) {
        PhoneInput input;
        if (xQueueReceive(phone_queue, &input, 0) == pdTRUE) {
            uint8_t previous_sensor_page = phone_control.latest.show_sensor;
            PhoneControl_Apply(&phone_control, &input,
                               (uint32_t)xTaskGetTickCount());
            if (!phone_control.latest.show_sensor ||
                previous_sensor_page != phone_control.latest.show_sensor)
                dirty = 1;
        }
        uint8_t bytes[256];
        int n = uart_read_bytes(UART_NUM_1, bytes, sizeof(bytes), pdMS_TO_TICKS(20));
        for (int i = 0; i < n; ++i) {
            if (display_feed(&parser, bytes[i], sensor_screen)) {
                last_rx = xTaskGetTickCount();
                received = 1; stale = 0;
                if (phone_control.latest.show_sensor) dirty = 1;
            }
        }
        TickType_t now = xTaskGetTickCount();
        if (received && !stale && (TickType_t)(now - last_rx) > pdMS_TO_TICKS(2000)) {
            memset(sensor_screen, 0, sizeof(sensor_screen));
            strcpy(sensor_screen[0], "STM32 LINK LOST");
            strcpy(sensor_screen[3], "CHECK UART / POWER");
            stale = 1;
            if (phone_control.latest.show_sensor) dirty = 1;
        }
        int phone_is_online = PhoneControl_Online(&phone_control,
                                                 (uint32_t)now, control_timeout);
        if (!phone_control.latest.show_sensor &&
            phone_is_online != phone_was_online)
            dirty = 1;
        phone_was_online = phone_is_online;
        if (!online && (TickType_t)(now - last_retry) >= pdMS_TO_TICKS(2000)) {
            last_retry = now;
            online = start_oled(); dirty = 1;
            if (!online) ESP_LOGW(TAG, "OLED offline; retry in 2 seconds");
        }
        if (online && dirty && (TickType_t)(now - last_draw) >= pdMS_TO_TICKS(200)) {
            last_draw = now;
            if (!phone_control.latest.show_sensor)
                PhoneControl_Render(&phone_control, (uint32_t)now,
                                    control_timeout, CONFIG_QUAD_AP_SSID, phone_screen);
            char (*rows)[DISPLAY_COLS + 1] = phone_control.latest.show_sensor ?
                sensor_screen : phone_screen;
            if (draw_screen(rows) != ESP_OK) {
                online = 0; last_retry = now;
                ESP_LOGW(TAG, "OLED write failed");
            } else dirty = 0;
        }
    }
}
