idf_component_register(
    SRCS "src/dev_tester_app.cpp" "src/dev_tester_icon.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl crystal_app crystal_shell
)
