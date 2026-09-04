idf_component_register(
    SRCS "src/crystal_core.cpp"
    INCLUDE_DIRS "include"
    REQUIRES crystal_hal lvgl esp_netif esp_timer
)
