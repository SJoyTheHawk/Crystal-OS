idf_component_register(
    SRCS "src/crystal_http.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_timer esp_http_client mbedtls
)
