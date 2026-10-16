idf_component_register(SRCS "camera_stream.cc" "web_preview.c" "tca9555.c" "camera_power.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_cam esp_driver_i2c esp_driver_gpio esp_psram
             nvs_flash esp_wifi esp_netif esp_event esp_http_server esp_timer
             espressif__esp_new_jpeg)
