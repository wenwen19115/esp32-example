idf_component_register(SRCS "main.c" "web_preview.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES
                        usb
                        esp_psram
                        nvs_flash
                        esp_wifi
                        esp_netif
                        esp_event
                        esp_http_server
                    )
