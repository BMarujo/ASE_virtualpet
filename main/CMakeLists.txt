idf_component_register(SRCS "main.c" "driver_dht20.c" "assets/pet_assets.c"
                    INCLUDE_DIRS "." "assets"
                    REQUIRES st7735_driver esp_wifi nvs_flash esp_event esp_netif mqtt fatfs sdmmc vfs esp_adc)
