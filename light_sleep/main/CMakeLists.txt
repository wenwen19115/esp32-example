idf_component_register(SRCS "light_sleep_example_main.c"
                       PRIV_REQUIRES esp_driver_uart esp_driver_gpio esp_psram
                       INCLUDE_DIRS ".")
