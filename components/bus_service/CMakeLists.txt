idf_component_register(
    SRCS "src/bus_service.c"
         "src/bus_normalize.c"
         "src/bus_provider_ctb.c"
         "src/bus_provider_kmb.c"
         "src/bus_provider_ctb_stop.c"
         "src/bus_stop_metadata_common.c"
         "src/bus_stop_discovery.c"
         "src/bus_stop_catalog.c"
         "src/bus_catalog_sync.c"
         "src/bus_routes.c"
         "src/bus_index_data.c"
    INCLUDE_DIRS "include"
    REQUIRES crystal_hal crystal_http esp_http_client nvs_flash heap esp-tls esp_timer mbedtls spiffs espressif__cjson lvgl__lvgl espressif__esp_lvgl_port
)

set(CRYSTAL_BUS_CATALOG_MANIFEST_URL "https://raw.githubusercontent.com/SJoyTheHawk/Crystal-OS/92fd21a/artifacts/bus_catalog/full/manifest.json" CACHE STRING
    "HTTPS URL of the published stop catalog manifest.json")
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "CRYSTAL_BUS_CATALOG_MANIFEST_URL=\"${CRYSTAL_BUS_CATALOG_MANIFEST_URL}\"")

set(CRYSTAL_BUS_CATALOG_TEST_FAULT "none" CACHE STRING
    "4R.3 test fault: none, interrupt, corrupt, or no_space")
set_property(CACHE CRYSTAL_BUS_CATALOG_TEST_FAULT PROPERTY STRINGS none interrupt corrupt no_space)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "CRYSTAL_BUS_CATALOG_TEST_FAULT=\"${CRYSTAL_BUS_CATALOG_TEST_FAULT}\"")

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
