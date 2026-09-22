idf_component_register(SRCS "oled_main.c" "nrf24_esp32.c"
    INCLUDE_DIRS "." "../../Common"
    REQUIRES esp_driver_i2c esp_driver_uart esp_driver_spi esp_driver_gpio)
