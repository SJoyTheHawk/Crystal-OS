idf_component_register(
    SRCS "src/bus_service.c"
         "src/bus_routes.c"
         "src/bus_index_data.c"
    INCLUDE_DIRS "include"
    REQUIRES crystal_hal crystal_http esp_http_client nvs_flash heap esp-tls esp_timer espressif__cjson lvgl__lvgl espressif__esp_lvgl_port
)

set(CRYSTAL_HTTP_PHASE3_KMB_FORCE_FAILURE OFF CACHE BOOL
    "Use an invalid KMB endpoint for the Phase 3 retry validation run")
if(CRYSTAL_HTTP_PHASE3_KMB_FORCE_FAILURE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        CRYSTAL_HTTP_PHASE3_KMB_FORCE_FAILURE=1)
endif()
