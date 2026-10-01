idf_component_register(
    SRCS "src/bus_service.c"
         "src/bus_normalize.c"
         "src/bus_provider_ctb.c"
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

set(CRYSTAL_HTTP_PHASE4_KMB_STOPS_FORCE_FAILURE OFF CACHE BOOL
    "Use an invalid KMB route-stop endpoint for Phase 4 Step 8 validation")
if(CRYSTAL_HTTP_PHASE4_KMB_STOPS_FORCE_FAILURE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        CRYSTAL_HTTP_PHASE4_KMB_STOPS_FORCE_FAILURE=1)
endif()

set(CRYSTAL_HTTP_PHASE4_KMB_STOPS_FORCE_BODY_LIMIT OFF CACHE BOOL
    "Use a 1 KiB KMB stop body limit for Phase 4 Step 8 validation")
if(CRYSTAL_HTTP_PHASE4_KMB_STOPS_FORCE_BODY_LIMIT)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        CRYSTAL_HTTP_PHASE4_KMB_STOPS_FORCE_BODY_LIMIT=1)
endif()

set(CRYSTAL_HTTP_PHASE5_CTB_STOPS_FORCE_FAILURE OFF CACHE BOOL
    "Use an invalid CTB route-stop endpoint for the Phase 5 Step 3 validation")
if(CRYSTAL_HTTP_PHASE5_CTB_STOPS_FORCE_FAILURE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        CRYSTAL_HTTP_PHASE5_CTB_STOPS_FORCE_FAILURE=1)
endif()

set(CRYSTAL_HTTP_PHASE5_CTB_ROUTE_FORCE_FAILURE OFF CACHE BOOL
    "Use an invalid CTB route endpoint for the Phase 5 Slice 2 handoff validation")
if(CRYSTAL_HTTP_PHASE5_CTB_ROUTE_FORCE_FAILURE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        CRYSTAL_HTTP_PHASE5_CTB_ROUTE_FORCE_FAILURE=1)
endif()
