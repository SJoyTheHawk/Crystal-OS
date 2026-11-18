idf_component_register(
    SRCS "src/crystal_wifi_diagnostics.cpp"
    INCLUDE_DIRS "include"
    REQUIRES crystal_hal esp_netif esp_wifi esp_http_client mbedtls esp_app_format
             esp_timer freertos lwip log heap
)
if(IDF_TARGET STREQUAL "esp32p4")
    idf_component_optional_requires(PRIVATE espressif__esp_hosted)
endif()
