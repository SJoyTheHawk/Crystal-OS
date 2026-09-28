idf_component_register(
    SRCS "src/bus_service.c"
         "src/bus_routes.c"
         "src/bus_index_data.c"
    INCLUDE_DIRS "include"
    REQUIRES crystal_hal crystal_http esp_http_client nvs_flash heap esp-tls esp_timer espressif__cjson lvgl__lvgl espressif__esp_lvgl_port
)
