idf_component_register(
    SRCS "src/crystal_shell.cpp" "src/crystal_keyboard.cpp"
    INCLUDE_DIRS "include"
    REQUIRES crystal_registry crystal_hal crystal_app crystal_core weather_app esp-brookesia lvgl
)
