idf_component_register(
    SRCS "src/calculator_app.cpp" "src/calculator_icon.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl crystal_app
)
