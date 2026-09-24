#include "phone_server.h"
#include "phone_control.h"
#include <string.h>
#include "sdkconfig.h"
#include "driver/uart.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "phone_server";
static PhoneControl transmit_state;
extern const char page_start[] asm("_binary_phone_control_html_start");
extern const char page_end[] asm("_binary_phone_control_html_end");

static esp_err_t page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    size_t length = (size_t)(page_end - page_start);
    if (length && page_start[length - 1U] == '\0') --length;
    return httpd_resp_send(req, page_start, length);
}

static esp_err_t command_handler(httpd_req_t *req)
{
    char marker[4] = {0};
    char body[65];
    PhoneInput input;
    if (httpd_req_get_hdr_value_str(req, "X-Quad-Control", marker,
                                     sizeof(marker)) != ESP_OK ||
        strcmp(marker, "1") != 0) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Missing controller header");
        return ESP_OK;
    }
    if (req->content_len == 0 || req->content_len > 64) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid command size");
        return ESP_OK;
    }
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) {
            if (n == HTTPD_SOCK_ERR_TIMEOUT) httpd_resp_send_408(req);
            return ESP_FAIL;
        }
        received += (size_t)n;
    }
    if (!PhoneControl_Parse(body, received, &input)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid command values");
        return ESP_OK;
    }
    PhoneControl_Apply(&transmit_state, &input, (uint32_t)xTaskGetTickCount());
    uint8_t frame[CONTROL_FRAME_SIZE];
    PhoneControl_Encode(&transmit_state, frame);
    if (uart_write_bytes(UART_NUM_1, (const char *)frame,
                         CONTROL_FRAME_SIZE) != CONTROL_FRAME_SIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "STM32 UART send failed");
        return ESP_OK;
    }
    input.sequence = transmit_state.latest.sequence;
    xQueueOverwrite((QueueHandle_t)req->user_ctx, &input);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "OK");
}

esp_err_t PhoneServer_Start(QueueHandle_t command_queue)
{
    size_t ssid_len = strlen(CONFIG_QUAD_AP_SSID);
    size_t pass_len = strlen(CONFIG_QUAD_AP_PASSWORD);
    if (!command_queue || ssid_len == 0 || ssid_len > 32 ||
        pass_len < 8 || pass_len > 63) return ESP_ERR_INVALID_ARG;

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    if (!esp_netif_create_default_wifi_ap()) return ESP_FAIL;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    wifi_config_t config = {0};
    memcpy(config.ap.ssid, CONFIG_QUAD_AP_SSID, ssid_len);
    config.ap.ssid_len = (uint8_t)ssid_len;
    memcpy(config.ap.password, CONFIG_QUAD_AP_PASSWORD, pass_len);
    config.ap.channel = 6;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.max_connection = 1;
    config.ap.pmf_cfg.required = true;
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.stack_size = 8192;
    server_config.max_uri_handlers = 2;
    httpd_handle_t server = NULL;
    err = httpd_start(&server, &server_config);
    if (err != ESP_OK) return err;
    httpd_uri_t page = {.uri = "/", .method = HTTP_GET,
                        .handler = page_handler};
    httpd_uri_t command = {.uri = "/api/control", .method = HTTP_POST,
                           .handler = command_handler, .user_ctx = command_queue};
    err = httpd_register_uri_handler(server, &page);
    if (err != ESP_OK) return err;
    err = httpd_register_uri_handler(server, &command);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "Phone controller AP %s at http://192.168.4.1/",
             CONFIG_QUAD_AP_SSID);
    return ESP_OK;
}
