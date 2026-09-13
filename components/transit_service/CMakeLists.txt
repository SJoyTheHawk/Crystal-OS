idf_component_register(
    SRCS "src/transit_service.c"
         "src/transit_http.c"
         "src/transit_json.c"
         "src/transit_routes.c"
         "src/transit_index.c"
         "src/transit_index_data.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client esp-tls mbedtls nvs_flash esp_netif freertos
)
