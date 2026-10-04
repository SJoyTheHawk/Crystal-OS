idf_component_register(
    SRCS "src/clock_app.cpp" "src/clock_icon.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl crystal_app crystal_core
)
