idf_component_register(SRCS "station_example_main.c"
                    PRIV_REQUIRES esp_wifi nvs_flash
                    esp_http_client INCLUDE_DIRS ".")
