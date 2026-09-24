idf_component_register(SRCS "oled_main.c" "phone_control.c" "phone_server.c"
    INCLUDE_DIRS "." "../../Common"
    EMBED_TXTFILES "phone_control.html"
    REQUIRES esp_driver_i2c esp_driver_uart esp_wifi esp_netif esp_event
             esp_http_server nvs_flash)
