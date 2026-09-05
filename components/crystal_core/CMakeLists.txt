idf_component_register(
    SRCS "src/crystal_core.cpp"
    INCLUDE_DIRS "include"
    REQUIRES crystal_hal lvgl esp_event esp_netif esp_timer esp_http_client mbedtls
)
