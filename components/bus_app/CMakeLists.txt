idf_component_register(
    SRCS "src/bus_app.cpp" "src/bus_keypad.cpp" "src/bus_icon.c"
    INCLUDE_DIRS "include"
    REQUIRES crystal_app crystal_core transit_service lvgl__lvgl
)
