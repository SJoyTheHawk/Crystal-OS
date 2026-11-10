idf_component_register(
    SRCS "src/crystal_core.cpp" "src/crystal_core_http.cpp"
    INCLUDE_DIRS "include"
    REQUIRES crystal_hal crystal_http lvgl esp_event esp_netif esp_timer esp_pm app_update
)
