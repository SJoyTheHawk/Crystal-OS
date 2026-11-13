idf_component_register(
    SRCS "src/crystal_control.cpp"
    INCLUDE_DIRS "include"
    REQUIRES crystal_hal crystal_registry crystal_signature crystal_core esp_http_server esp_system espressif__cjson esphome__libsodium espressif__mdns spiffs esp_driver_usb_serial_jtag
)
