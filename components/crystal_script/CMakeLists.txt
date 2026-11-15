idf_component_register(
    SRCS "src/crystal_script.cpp"
    INCLUDE_DIRS "include"
    REQUIRES crystal_app crystal_package crystal_core crystal_hal crystal_http esp_timer lvgl espressif__cjson georgik__lua
)
