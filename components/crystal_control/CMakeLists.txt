idf_component_register(
    SRCS "src/crystal_control.cpp"
    INCLUDE_DIRS "include"
    REQUIRES crystal_hal crystal_registry crystal_signature esp_http_server espressif__cjson esphome__libsodium spiffs
)
