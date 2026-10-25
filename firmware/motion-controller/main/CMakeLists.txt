idf_component_register(SRCS "main.cc"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_uart
                                esp_timer cjson mqtt esp_wifi esp_event esp_netif nvs_flash)

