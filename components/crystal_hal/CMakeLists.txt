idf_component_register(
    SRCS "src/crystal_hal.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp32_s3_touch_lcd_4b nvs_flash esp_wifi esp_netif esp_event esp_lcd_touch lvgl
)
