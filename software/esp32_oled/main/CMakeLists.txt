idf_component_register(SRCS "oled_main.c"
    INCLUDE_DIRS "." "../../Common"
    REQUIRES esp_driver_i2c esp_driver_uart)
