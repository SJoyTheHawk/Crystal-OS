set(CRYSTAL_HAL_BOARD_COMPONENT esp32_s3_touch_lcd_4b)
set(CRYSTAL_HAL_WIFI_COMPONENT esp_wifi)
set(CRYSTAL_HAL_TOUCH_COMPONENT esp_lcd_touch)
if(IDF_TARGET STREQUAL "esp32p4")
    set(CRYSTAL_HAL_BOARD_COMPONENT crystal_board_p4)
    set(CRYSTAL_HAL_WIFI_COMPONENT esp_wifi esp_wifi_remote)
    set(CRYSTAL_HAL_TOUCH_COMPONENT)
    set(CRYSTAL_HAL_ETH_COMPONENT esp_eth ip101)
endif()

idf_component_register(
    SRCS "src/crystal_hal.cpp" "src/crystal_ethernet.cpp" "src/crystal_network_policy.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ${CRYSTAL_HAL_BOARD_COMPONENT} esp_codec_dev nvs_flash ${CRYSTAL_HAL_WIFI_COMPONENT} esp_netif esp_event ${CRYSTAL_HAL_ETH_COMPONENT} ${CRYSTAL_HAL_TOUCH_COMPONENT} esp_driver_gpio spiffs app_update lvgl
)
